import EmProcess from "./EmProcess.mjs";
import BrotliModule from "./brotli/brotli.mjs";

export default class BrotliProcess extends EmProcess {
    constructor(opts) {
        super(BrotliModule, { ...opts });
    }
};
