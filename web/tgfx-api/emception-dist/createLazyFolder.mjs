// Modified version of createLazyFile from Emscripten's FS
// https://github.com/emscripten-core/emscripten/blob/main/src/library_fs.js

function doFetchSync(url) {
    // TODO: Use mozResponseArrayBuffer, responseStream, etc. if available.
    const xhr = new XMLHttpRequest();
    xhr.open("GET", url, false);

    // Some hints to the browser that we want binary data.
    xhr.responseType = "arraybuffer";
    xhr.overrideMimeType?.("text/plain; charset=x-user-defined");

    xhr.send(null);
    if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304)) {
        throw new Error(`Couldn't load ${url}. Status: ${xhr.status}`);
    }
    if (!xhr.response) {
        throw new Error(`Couldn't load ${url}. No xhr response.`);
    }
    return new Uint8Array(xhr.response);
}

async function doFetchAsync(url) {
    const res = await fetch(url);
    const buf = await res.arrayBuffer();
    return new Uint8Array(buf);
}

export function doFetch(url, async = false) {
    return async ? doFetchAsync(url) : doFetchSync(url);
}

if (!globalThis.XMLHttpRequest) {
    throw new Error("Cannot do synchronous binary XHRs outside webworkers in modern browsers.");
}

export default function createLazyFolder(FS, path, mode, loadFnc) {
    if (FS.analyzePath(path).exists) return;

    let loaded = false;

    var node = FS.mkdir(path, mode);
    let { contents } = node;

    const ensure_content = () =>  {
        if (loaded) return;
        try {
            loaded = true;
            loadFnc();
        } catch (e) {
            loaded = false;
            throw e;
        }
    };

    Object.defineProperties(node, {
        contents: {
            get: () => {
                ensure_content();
                return contents;
            },
            set: (val) => {
                ensure_content();
                contents = val;
            },
        },
        loaded: {
            get: () => {
                return loaded;
            },
        },
    });

    const original_lookup = node.node_ops.lookup;
    node.node_ops = {
        ...node.node_ops,
        lookup: (parent, name) => {
            ensure_content();
            node.node_ops.lookup = original_lookup;
            return FS.lookupNode(parent, name);
        }
    };

    return node;
}
