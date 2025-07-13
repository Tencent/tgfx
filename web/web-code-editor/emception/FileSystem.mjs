import EmProcess from "./EmProcess.mjs";
import WasmPackageModule from "./wasm-package/wasm-package.mjs";
import createLazyFolder, { doFetch } from "./createLazyFolder.mjs"
import BrotliProcess from "./BrotliProcess.mjs";

export default class FileSystem extends EmProcess {
    _brotli = null;
    _cache = null;

    constructor({ cache = "/cache", ...opts } = {}) {
        super(WasmPackageModule, { ...opts });
        this.init.push(this.#init.bind(this, cache, opts));
    }

    async #init(cache, opts) {
        this._brotli = await new BrotliProcess({ FS: this.FS, ...opts});
        while (cache.endsWith("/")) {
            cache = cache.slice(0, -1);
        }
        this._cache = cache;
        if (!this.exists(cache)) {
            this.persist(cache);
        }
        await this.pull();
    }

    unpack(path, cwd = "/") {
        if (path.endsWith(".br")) {
            // it's a brotli file, decompress it first
            this._brotli.exec(["brotli", "--decompress", "-o", "/tmp/archive.pack", path], { cwd: "/tmp/" });
            this.exec(["wasm-package", "unpack", "/tmp/archive.pack"], { cwd });
            this.FS.unlink("/tmp/archive.pack");
        } else {
            this.exec(["wasm-package", "unpack", path], { cwd });
        }
    }

    async #cachedDownload(url, async = false) {

        const cache = this._cache;
        const hash = btoa(url).replace(/\+/g, "-").replace(/\//g, "_").replace(/=/g, "");
        const ext = url.replace(/^.*?(\.[^\.]+)?$/, "$1");
        const cache_file = `${cache}/${hash}${ext}`;

        if (this.exists(cache_file))
            return cache_file;
        let data = await doFetch(url, async);

        this.writeFile(cache_file, data);
        await this.push();
        return cache_file;
    }

    #ignorePermissions(f) {
        const { ignorePermissions } = this.FS;
        this.FS.ignorePermissions = true;
        try {
            return f();
        } finally {
            this.FS.ignorePermissions = ignorePermissions;
        }
    }

    #lazyLoads = new Map();
    #lazyLoadsDone = new Set();

    #lazyLoad(path, url, packaged = false, async = false) {

        if (this.#lazyLoadsDone.has(url)) return;

        return this.#cachedDownload(url, async).then((cache_file) => {
            this.#ignorePermissions(() => {
                if (this.#lazyLoadsDone.has(url)) return;
                this.#lazyLoadsDone.add(url);
                try {
                    if (packaged) {
                        this.unpack(cache_file, path);
                    } else {
                        const data = this.readFile(cache_file);
                        this.writeFile(path, data);
                    }
                } catch (e) {
                    this.#lazyLoadsDone.delete(url);
                    throw e;
                }
            });
        });
    }

    preloadLazy(path, async = true) {
        const [root, url, packaged] = this.#lazyLoads.get(path);
        return this.#lazyLoad(root, url, packaged, async);
    }

    cachedLazyFolder(path, url, mode = 0o777, package_root = path) {
        this.#lazyLoads.set(path, [package_root, url, true]);
        this.#ignorePermissions(() => {
            createLazyFolder(this.FS, path, mode, () => this.preloadLazy(path, false));
        });
    }

    persist(path) {
        this.FS.mkdirTree(path);
        this.FS.mount(this.FS.filesystems.IDBFS, {}, path);
    }

    exists(path) {
        return this.analyzePath(path).exists;
    }
    analyzePath(...args) {
        return this.FS.analyzePath(...args)
    }
    mkdirTree(...args) {
        return this.FS.mkdirTree(...args)
    }
    mkdir(...args) {
        return this.FS.mkdir(...args)
    }
    unlink(...args) {
        return this.FS.unlink(...args)
    }
    readFile(...args) {
        return this.FS.readFile(...args)
    }
    writeFile(...args) {
        return this.FS.writeFile(...args)
    }
    symlink(oldPath, newPath) {
        return this.FS.symlink(oldPath, newPath);
    }

    #pull = null;
    #pullRequested = false;
    pull() {
        if (this.#pull) {
            this.#pullRequested = true;
            return this.#pull;
        }
        this.#pullRequested = false;
        this.#pull = new Promise((resolve, reject) => this.FS.syncfs(true, (err) => {
            this.#pull = null;
            if (this.#pullRequested) this.pull();
            if (err) {
                reject(err);
            } else {
                resolve();
            }
        }));
        return this.#pull;
    }

    async push() {
        return await new Promise((resolve, reject) => this.FS.syncfs(false, function (err) {
            if (err) {
                reject(err);
            } else {
                resolve();
            }
        }));
    }
};
