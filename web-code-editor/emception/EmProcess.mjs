import Process from "./Process.mjs";
import AsyncInitializable from "./AsyncInitializable.mjs";

export default class EmProcess extends AsyncInitializable(Process) {
    _module = null;
    _memory = null;

    _print = (...args) => console.log(...args);
    _printErr = (...args) => console.warn(...args);

    constructor(Module, {
        FS,
        onrunprocess,
        onprint,
        onprintErr,
        ...opts
    } = {}) {
        super({
            onrunprocess,
            onprint,
            onprintErr,
        });
        this.init.push(() => this.#init(Module, FS, { onrunprocess, ...opts }));
    }

    async #init(Module, FS, opts) {
        const fsroot = FS && {
            ROOT: {
                type: "PROXYFS",
                opts: {
                    root: "/",
                    fs: FS,
                },
            }
        };
        this._module = await new Module({
            ...opts,
            ...fsroot,
            noInitialRun: true,
            noExitRuntime: true,
            print: (...args) => this._print(...args),
            printErr: (...args) => this._printErr(...args),
        });
        this._memory = this._module.HEAPU8.slice();

        if (fsroot || opts.ROOT) {
            // Do not cache nodes belonging to the PROXYFS mountpoint.
            const hashAddNode = this._module.FS.hashAddNode;
            this._module.FS.hashAddNode = (node) => {
                if (node?.mount?.mountpoint === "/") return;
                hashAddNode(node);
            }
        }

        this.#improveErrnoError();
    }

    #improveErrnoError() {
        const FS = this._module.FS;
        const ERRNO_CODES = Object.fromEntries(
            Object.entries(this._module.ERRNO_CODES)
                .map(([k, v]) => [v, k])
        );
        FS.ErrnoError = class ErrnoError extends Error {
            constructor(errno, node) {
                super(`FS error: ${ERRNO_CODES[errno]}`);
                this.node = node;
                this.setErrno(errno);
            }

            setErrno(errno) {
                this.errno = parseInt(errno);
            }

            get code() {
                return ERRNO_CODES[this.errno];
            }
        }
        Object.keys(FS.genericErrors).forEach((code) => {
            code = parseInt(code);
            Object.defineProperty(FS.genericErrors, code, {
                get: () => new FS.ErrnoError(code)
            });
        });
    }

    get FS() {
        return this._module.FS;
    }

    _callMain(argc, argv) {
        return this._module._main(argc, argv);
    }

    running = false;
    exec(args, opts = {}) {
        this.running = true;
        if ((typeof args) === "string") args = args.split(/ +/g);

        // Clang's driver uses global state, and this might not be the first time we run the module.
        // Reinitialize the memory to its initial state to reset the global state.
        // TODO: Is this safe? Is this missing some other source of state? wasm globals? JS?
        this._module.HEAPU8.fill(0);
        this._module.HEAPU8.set(this._memory);

        // Allocate argv
        const argc = args.length;
        const argv = this._module._malloc((argc + 1) * 4);
        const allocs = [argv];
        for (let i = 0; i < argc; i++) {
            const p = this._module.HEAP32[(argv >> 2) + i] = this._module.allocateUTF8(args[i]);
            allocs.push(p);
        }
        this._module.HEAP32[(argv >> 2) + argc] = 0;

        let returncode = 0;
        const stdout = [];
        const stderr = [];

        this._print = (...args) => {
            this.onprint(...args);
            opts.print && opts.print(...args);
            stdout.push(...args);
        };
        this._printErr = (...args) => {
            this.onprintErr(...args);
            opts.printErr && opts.printErr(...args);
            stderr.push(...args);
        };

        try {
            if (opts.cwd) this.cwd = opts.cwd;
            returncode = this._module._main(argc, argv);
        } catch (e) {
            if (typeof e === "number") {
                returncode = -84;
            } else if ("status" in e) {
                returncode = e.status;
            } else {
                returncode = -42;
            }
        } finally {
            allocs.forEach(p => this._module._free(p));
        }

        this.running = false;

        return {
            returncode,
            stdout: stdout.join("\n"),
            stderr: stderr.join("\n"),
        }
    }
};
