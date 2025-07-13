export default class Process {
    constructor({
        onrunprocess = () => ({ returncode: 1, stdout: "", stderr: "Not implemented" }),
        onprint = () => {},
        onprintErr = () => {},
    }) {
        Object.assign(this, { onrunprocess, onprint, onprintErr });
    }

    onrunprocess = () => {};
    onprint = () => {};
    onprintErr = () => {};

    get FS() {
        throw new Error("unimplemented");
    }

    get cwd() {
        return this.FS.cwd();
    }

    set cwd(cwd) {
        this.FS.chdir(cwd);
    }
};
