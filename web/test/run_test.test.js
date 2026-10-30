// Copyright (C) 2026 Tencent. All rights reserved.

const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const SOURCE = fs.readFileSync(path.join(__dirname, 'run_test.js'), 'utf8');

function RunScenario(options) {
  return new Promise(function(resolve, reject) {
    const page = new EventEmitter();
    const client = new EventEmitter();
    const output = [];
    const extracted = [];
    const mutations = [];
    const published = [];
    const backend = options.backend || 'webgpu';
    const mode = options.mode || 'verify';
    const commit = 'a'.repeat(40);
    const files = new Set(options.files || []);
    function CheckMutation(file) {
      assert.ok(!file.includes(`${path.sep}baseline${path.sep}`), `Reference modified: ${file}`);
      mutations.push(file);
    }
    const filesystem = {
      existsSync(file) { return files.has(path.basename(file)); },
      mkdirSync: CheckMutation,
      rmSync: CheckMutation,
    };
    class FakeZip {
      getEntries() {
        return [{ entryName: options.zipEntry || 'image.webp' }];
      }
      extractAllTo(destination) {
        CheckMutation(destination);
        extracted.push(destination);
      }
    }
    const server = new EventEmitter();
    server.stdout = new EventEmitter();
    server.stderr = new EventEmitter();
    server.kill = function() {};
    client.send = async function(method, parameters) {
      assert.equal(method, 'Browser.setDownloadBehavior');
      assert.equal(parameters.eventsEnabled, true);
    };
    page.createCDPSession = async function() { return client; };
    page.goto = async function() {
      if (options.navigationFailure) {
        throw new Error('Navigation failed');
      }
      if (options.configuration !== null) {
        const configuration = options.configuration || { mode, backend, commit };
        page.emit('console', { text() { return 'Test configuration: ' + JSON.stringify(configuration); } });
      }
      if (mode === 'reference') {
        const stats = options.stats || [2, 2, 2];
        for (const message of [
          `[==========] Running ${stats[0]} tests from 1 test suite.`,
          `[==========] ${stats[1]} tests from 1 test suite ran. (1 ms total)`,
          `[  PASSED  ] ${stats[2]} tests.`,
        ]) {
          page.emit('console', { text() { return message; } });
        }
      }
      for (const message of options.messages || []) {
        page.emit('console', { text() { return message; } });
      }
      if (options.pageError) {
        page.emit('pageerror', new Error('Page failed'));
      }
      if (options.cancelDownload) {
        client.emit('Browser.downloadWillBegin', { guid: 'download' });
        client.emit('Browser.downloadProgress', { guid: 'download', state: 'canceled' });
      }
    };
    const browser = {
      async newPage() { return page; },
      async close() {},
    };
    const modules = {
      child_process: {
        spawn() {
          queueMicrotask(function() { server.stdout.emit('data', 'Hit CTRL-C to stop'); });
          return server;
        },
      },
      path,
      fs: filesystem,
      puppeteer: { async launch() { return browser; } },
      'adm-zip': FakeZip,
      './reference_cache': {
        PublishReference(zip, selectedBackend, revision, tests) {
          assert.equal(mode, 'reference');
          if (options.publishFailure) throw new Error('Invalid reference archive');
          published.push({ zip, backend: selectedBackend, commit: revision, tests });
        },
      },
    };
    const sandbox = {
      __dirname,
      require(name) {
        assert.ok(Object.hasOwn(modules, name), `Unexpected module: ${name}`);
        return modules[name];
      },
      process: {
        argv: ['node', 'run_test.js', `--backend=${backend}`, `--mode=${mode}`].concat(
          mode === 'reference' ? [`--reference-commit=${commit}`,
            `--web-root=${path.join(__dirname, `build-reference-${backend}`, 'session/runtime')}`] : []),
        platform: 'darwin',
        exit(code) { resolve({ code, output, extracted, mutations, published }); },
      },
      console: {
        log(...args) { output.push(args.join(' ')); },
        warn(...args) { output.push(args.join(' ')); },
        error(...args) { output.push(args.join(' ')); },
      },
      setTimeout(callback, delay) {
        if (delay <= 1000 || (options.pageTimeout && delay === 60000)) {
          queueMicrotask(callback);
        }
        return 0;
      },
    };
    try {
      vm.runInNewContext(SOURCE, sandbox, { filename: 'run_test.js' });
    } catch (error) {
      reject(error);
    }
  });
}

const SUCCESS = 'All tests finished with exit code: 0';
const DONE = 'Test page actions finished';
const GPU_ERROR = '[WebGPU] Uncaptured Validation error: invalid attachment';
const CASES = [
  ['empty output succeeds', { messages: [SUCCESS, DONE] }, 0],
  ['missing artifact configuration fails', { messages: [SUCCESS, DONE], configuration: null }, 1],
  ['verification refuses generation artifact', { messages: [SUCCESS, DONE], configuration: { mode: 'reference', backend: 'webgpu' } }, 1],
  ['verification ignores reference archive', { messages: [SUCCESS, 'Download started: reference-cache.zip', DONE], files: ['reference-cache.zip'] }, 0],
  ['GPU failure before GTest success is retained', { messages: [GPU_ERROR, SUCCESS, DONE] }, 1],
  ['GPU failure after GTest success is retained', { messages: [SUCCESS, GPU_ERROR, DONE] }, 1],
  ['GTest failure is retained', { messages: ['All tests finished with exit code: 1', SUCCESS, DONE] }, 1],
  ['page error fails', { messages: [SUCCESS], pageError: true }, 1],
  ['navigation error fails', { navigationFailure: true }, 1],
  ['page completion timeout fails', { messages: [SUCCESS], pageTimeout: true }, 1],
  ['missing declared download fails', { messages: [SUCCESS, 'Download started: test-out.zip', DONE] }, 1],
  ['canceled download fails', { messages: [SUCCESS, DONE], cancelDownload: true }, 1],
  ['diagnostic archive is extracted', { messages: [SUCCESS, 'Download started: baseline-out.zip', DONE], files: ['baseline-out.zip'] }, 0],
  ['old cache archive is never imported', { messages: [SUCCESS, DONE], files: ['cache-web.zip'] }, 0],
  ['archive traversal is rejected', { messages: [SUCCESS, DONE], files: ['test-out.zip'], zipEntry: '../baseline/version.json' }, 1],
];

const REFERENCE_CASES = [
  ['complete generation publishes', {}, 0],
  ['GPU error blocks publishing', { messages: [GPU_ERROR] }, 1],
  ['GTest failure blocks publishing', { messages: ['All tests finished with exit code: 1'] }, 1],
  ['empty suite blocks publishing', { stats: [0, 0, 0] }, 1],
  ['incomplete suite blocks publishing', { stats: [3, 2, 2] }, 1],
  ['skipped test blocks publishing', { messages: ['[  SKIPPED ] SomeTest.Case'] }, 1],
  ['missing archive blocks publishing', { files: [] }, 1],
  ['stale commit blocks publishing', { configuration: { mode: 'reference', backend: 'webgpu', commit: 'b'.repeat(40) } }, 1],
  ['verification artifact blocks publishing', { configuration: { mode: 'verify', backend: 'webgpu', commit: 'a'.repeat(40) } }, 1],
  ['publication failure fails runner', { publishFailure: true }, 1],
];
for (const backend of ['webgl', 'webgpu']) {
  for (const [name, scenario, exitCode] of REFERENCE_CASES) {
    test(`${backend} reference ${name}`, { timeout: 2000 }, async function() {
      const options = { mode: 'reference', backend, files: ['reference-cache.zip'], ...scenario };
      options.messages = [...(scenario.messages || []), SUCCESS, 'Download started: reference-cache.zip', DONE];
      const result = await RunScenario(options);
      assert.equal(result.code, exitCode, result.output.join('\n'));
      assert.equal(result.published.length, exitCode === 0 ? 1 : 0);
      if (exitCode === 0) {
        assert.equal(result.published[0].backend, backend);
        assert.equal(result.published[0].tests, 2);
      }
    });
  }
}

for (const [name, scenario, exitCode] of CASES) {
  test(name, { timeout: 2000 }, async function() {
    const result = await RunScenario(scenario);
    assert.equal(result.code, exitCode, result.output.join('\n'));
    assert.equal(result.published.length, 0);
    if (name === 'old cache archive is never imported') {
      assert.equal(result.extracted.length, 0);
    }
    if (name === 'diagnostic archive is extracted') {
      assert.deepEqual(result.extracted, [path.resolve(__dirname, '../../test/baseline-out')]);
    }
    if (name === 'empty output succeeds') {
      assert.ok(result.output.some(function(line) { return line.includes('not generated (no output files)'); }));
    }
  });
}

const PAGE_SOURCE = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8')
  .match(/<script type="module">([\s\S]*?)<\/script>/)[1].replace(/^\s*import .+;$/gm, '');
async function RunPage(options) {
  const messages = [];
  const downloads = [];
  const calls = [];
  const files = [];
  const mode = options.mode || 'verify';
  const configuration = { mode, backend: 'webgpu', commit: 'a'.repeat(40), ...options.configuration };
  class Zip {
    file(name) { files.push(name); }
    async generateAsync() { return {}; }
  }
  const module = {
    tgfx: { setColorSpace() {} },
    FS: {
      stat(directory) {
        if (directory !== '/test/baseline/.cache/webgpu') throw new Error('No output');
        return {};
      },
      readFile(filename) {
        if (options.missingFile && filename.endsWith(options.missingFile)) throw new Error('Missing reference file');
        return Buffer.from('{}');
      },
    },
    ccall(name) {
      calls.push(name);
      return name === 'GetTestConfiguration' ? JSON.stringify(configuration) : options.exitCode || 0;
    },
  };
  await vm.runInNewContext(`(async function() { ${PAGE_SOURCE} })()`, {
    TGFXTest: async function() { return module; },
    TGFXBind() {},
    navigator: {},
    location: { search: `?mode=${mode}&backend=webgpu&commit=${'a'.repeat(40)}` },
    URLSearchParams,
    URL: { createObjectURL() { return 'blob:test'; }, revokeObjectURL() {} },
    JSZip: Zip,
    document: {
      createElement() { return { click() { downloads.push(this.download); } }; },
      body: { appendChild() {}, removeChild() {} },
    },
    console: {
      log(...args) { messages.push(args.join(' ')); },
      warn() {}, error() {},
    },
  });
  return { messages, downloads, calls, files };
}

const PAGE_CASES = [
  ['ordinary run exports no reference', {}, false, false],
  ['reference run exports both cache files', { mode: 'reference' }, true, false],
  ['failed reference run exports no reference', { mode: 'reference', exitCode: 1 }, false, true],
  ['reference rejects ordinary artifact', { mode: 'reference', configuration: { mode: 'verify' } }, false, true],
  ['ordinary run rejects reference artifact', { configuration: { mode: 'reference' } }, false, true],
  ['reference rejects another commit', { mode: 'reference', configuration: { commit: 'b'.repeat(40) } }, false, true],
  ['missing reference file fails export', { mode: 'reference', missingFile: 'md5.json' }, false, true],
];
for (const [name, options, exportsReference, fails] of PAGE_CASES) {
  test(`page ${name}`, async function() {
    const result = await RunPage(options);
    assert.equal(result.downloads.includes('reference-cache.zip'), exportsReference);
    assert.equal(result.messages.includes('All tests finished with exit code: 1'), fails);
    assert.equal(result.messages.at(-1), DONE);
    if (exportsReference) assert.deepEqual(result.files, ['md5.json', 'version.json']);
    if (options.configuration) assert.deepEqual(result.calls, ['GetTestConfiguration']);
  });
}
