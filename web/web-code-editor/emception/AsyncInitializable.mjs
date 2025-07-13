class Base {};

export default (BaseClass = Base) => class AsyncInitializable extends BaseClass {
    ready = null;
    init = [];

    constructor(...args) {
        super(...args);
        this.ready = this.#init();
        const promise = this.ready.then(() => {
            delete this.then;
            return this;
        });
        this.then = (...args) => promise.then(...args);
    }

    async #init() {
        await null;
        const { init } = this;
        delete this.init;
        for (const f of init) {
            await f();
        }
    }
};
