import EmProcess from "./EmProcess.mjs";

export default class BoxProcess extends EmProcess {
    #tool_mapping = {};

    constructor(Module, { tool_mapping = {}, ...opts} = {}) {
        super(Module, opts);
        this.#tool_mapping = tool_mapping;
    }

    exec(args, opts = {}) {
        if ((typeof args) === "string") args = args.split(/ +/g);
        let tool = args[0].replace(/^.*\//, "");
        tool = this.#tool_mapping[tool] || tool;
        return super.exec([tool, ...args], opts);
    }
};
