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
    };
    const sandbox = {
      __dirname,
      require(name) {
        assert.ok(Object.hasOwn(modules, name), `Unexpected module: ${name}`);
        return modules[name];
      },
      process: {
        argv: ['node', 'run_test.js', '--backend=webgpu'],
        platform: 'darwin',
        exit(code) { resolve({ code, output, extracted, mutations }); },
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

for (const [name, scenario, exitCode] of CASES) {
  test(name, { timeout: 2000 }, async function() {
    const result = await RunScenario(scenario);
    assert.equal(result.code, exitCode, result.output.join('\n'));
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
