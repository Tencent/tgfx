// Copyright (C) 2026 Tencent. All rights reserved.

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const test = require('node:test');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');

const SOURCE = fs.readFileSync(path.join(__dirname, 'reference_cache.js'), 'utf8');
const COMMIT = 'a'.repeat(40);
const VERSION = Buffer.from(JSON.stringify({ Suite: { Image: 'v1' } }));
const MD5 = Buffer.from(JSON.stringify({ Suite: { Image: 'b'.repeat(32) } }));

function CreateFixture(backend) {
  const directory = path.resolve(__dirname, '../../test/baseline/.cache', backend);
  const files = new Map();
  const directories = new Set();
  const writes = [];
  let sequence = 0;
  const fixture = { directory, files, directories, writes, fault: '', commit: COMMIT };
  fixture.entries = [
    { entryName: 'md5.json', data: MD5 },
    { entryName: 'version.json', data: VERSION },
  ];
  const filesystem = {
    readFileSync(file, encoding) {
      if (!files.has(file)) throw new Error('Missing file');
      const data = files.get(file);
      return encoding ? data.toString() : Buffer.from(data);
    },
    existsSync(file) { return files.has(file) || directories.has(file); },
    lstatSync() { return { isSymbolicLink() { return fixture.symlink || false; } }; },
    mkdirSync(file, options) {
      if (directories.has(file) && !options?.recursive) throw new Error('Already exists');
      directories.add(file);
      writes.push(file);
    },
    mkdtempSync(prefix) {
      const name = prefix + (++sequence);
      directories.add(name);
      return name;
    },
    writeFileSync(file, data) {
      if (fixture.fault === 'write' && file.endsWith('/version.json')) throw new Error('Write failed');
      files.set(file, Buffer.from(data));
      writes.push(file);
    },
    renameSync(from, to) {
      if (fixture.fault === 'rename' && path.basename(from).startsWith('.reference-')) {
        throw new Error('Rename failed');
      }
      for (const file of Array.from(files.keys())) {
        if (file.startsWith(from + path.sep)) {
          files.set(to + file.slice(from.length), files.get(file));
          files.delete(file);
        }
      }
      directories.delete(from);
      directories.add(to);
      writes.push(to);
    },
    rmSync(file) {
      for (const key of Array.from(files.keys())) {
        if (key === file || key.startsWith(file + path.sep)) files.delete(key);
      }
      directories.delete(file);
    },
    rmdirSync(file) { directories.delete(file); },
  };
  class FakeZip {
    getEntries() {
      return fixture.entries.map(function(entry) {
        return { ...entry, getData() { return entry.data; } };
      });
    }
  }
  const modules = {
    fs: filesystem,
    path,
    crypto,
    child_process: {
      execFileSync(command, args) {
        assert.equal(command, 'git');
        if (args[0] === 'merge-base') {
          if (args[2] !== fixture.commit) throw new Error('Not a main commit');
          return Buffer.alloc(0);
        }
        assert.equal(args[0], 'show');
        return VERSION;
      },
    },
    'adm-zip': FakeZip,
  };
  const module = { exports: {} };
  vm.runInNewContext(SOURCE, {
    __dirname, module, Buffer,
    console: { log() {} },
    require(name) {
      assert.ok(Object.hasOwn(modules, name), name);
      return modules[name];
    },
  }, { filename: 'reference_cache.js' });
  fixture.api = module.exports;
  return fixture;
}

for (const backend of ['webgl', 'webgpu']) {
  test(`${backend} replaces a complete cache without merging stale entries`, function() {
    const fixture = CreateFixture(backend);
    const { api, files, directory } = fixture;
    fixture.directories.add(directory);
    files.set(path.join(directory, 'stale.json'), Buffer.from('old'));
    api.PublishReference('reference.zip', backend, COMMIT, 5);
    assert.equal(api.IsReferenceCurrent(backend, COMMIT), true);
    assert.equal(files.has(path.join(directory, 'stale.json')), false);
    assert.deepEqual(files.get(path.join(directory, 'version.json')), VERSION);
    assert.equal(fixture.directories.has(directory + '.lock'), false);
  });

  for (const missing of ['md5.json', 'version.json', 'reference.json']) {
    test(`${backend} missing ${missing} invalidates the cache`, function() {
      const { api, files, directory } = CreateFixture(backend);
      api.PublishReference('reference.zip', backend, COMMIT, 5);
      files.delete(path.join(directory, missing));
      assert.equal(api.IsReferenceCurrent(backend, COMMIT), false);
    });
  }

  test(`${backend} tampered MD5 and changed main commit invalidate the cache`, function() {
    const fixture = CreateFixture(backend);
    const { api, files, directory } = fixture;
    api.PublishReference('reference.zip', backend, COMMIT, 5);
    fixture.commit = 'c'.repeat(40);
    assert.equal(api.IsReferenceCurrent(backend, fixture.commit), false);
    fixture.commit = COMMIT;
    files.set(path.join(directory, 'md5.json'), Buffer.from(JSON.stringify({ Suite: { Image: 'c'.repeat(32) } })));
    assert.equal(api.IsReferenceCurrent(backend, COMMIT), false);
  });

  for (const fault of ['write', 'rename']) {
    test(`${backend} failed ${fault} preserves the previous reference`, function() {
      const fixture = CreateFixture(backend);
      fixture.api.PublishReference('reference.zip', backend, COMMIT, 5);
      const before = new Map(fixture.files);
      fixture.fault = fault;
      assert.throws(function() { fixture.api.PublishReference('reference.zip', backend, COMMIT, 5); });
      assert.deepEqual(fixture.files, before);
      assert.equal(fixture.directories.has(fixture.directory + '.lock'), false);
      assert.equal(fixture.api.IsReferenceCurrent(backend, COMMIT), true);
    });
  }
}

const INVALID_ARCHIVES = [
  ['missing MD5', [{ entryName: 'version.json', data: VERSION }]],
  ['missing version', [{ entryName: 'md5.json', data: MD5 }]],
  ['traversal', [{ entryName: '../version.json', data: VERSION }]],
  ['symbolic link', [{ entryName: 'version.json', data: VERSION, attr: 0xa000 << 16 }]],
  ['duplicate entry', [{ entryName: 'md5.json', data: MD5 }, { entryName: 'md5.json', data: MD5 }]],
  ['empty MD5', [{ entryName: 'md5.json', data: Buffer.from('{}') }, { entryName: 'version.json', data: VERSION }]],
  ['different source version', [{ entryName: 'md5.json', data: MD5 }, { entryName: 'version.json', data: Buffer.from('{}') }]],
  ['invalid JSON', [{ entryName: 'md5.json', data: Buffer.from('invalid') }, { entryName: 'version.json', data: VERSION }]],
  ['malformed MD5', [{ entryName: 'md5.json', data: Buffer.from('{"Suite":{"Image":"invalid"}}') }, { entryName: 'version.json', data: VERSION }]],
  ['unknown comparison key', [{ entryName: 'md5.json', data: Buffer.from(JSON.stringify({ Missing: 'b'.repeat(32) })) }, { entryName: 'version.json', data: VERSION }]],
];
for (const [name, entries] of INVALID_ARCHIVES) {
  test(`invalid reference ${name} is rejected before any writes`, function() {
    const fixture = CreateFixture('webgpu');
    fixture.entries = entries;
    assert.throws(function() { fixture.api.PublishReference('bad.zip', 'webgpu', COMMIT, 5); });
    assert.equal(fixture.writes.length, 0);
  });
}

test('a non main commit cannot publish reference data', function() {
  const fixture = CreateFixture('webgpu');
  assert.throws(function() { fixture.api.PublishReference('reference.zip', 'webgpu', 'b'.repeat(40), 5); });
  assert.equal(fixture.writes.length, 0);
});

test('active reference publication lock is never removed by a second writer', function() {
  const fixture = CreateFixture('webgpu');
  fixture.directories.add(fixture.directory + '.lock');
  assert.throws(function() { fixture.api.PublishReference('reference.zip', 'webgpu', COMMIT, 5); });
  assert.equal(fixture.directories.has(fixture.directory + '.lock'), true);
  assert.equal(fixture.files.size, 0);
});

const LFS_SOURCE = fs.readFileSync(path.join(__dirname, 'reference_source.js'), 'utf8');
function CreateLFSFixture(data) {
  const module = { exports: {} };
  const calls = [];
  const modules = {
    fs: {}, path, crypto,
    child_process: {
      execFileSync(command, args, options) {
        calls.push({ command, args, options });
        return data;
      },
    },
  };
  vm.runInNewContext(LFS_SOURCE, {
    __dirname, module, process: { env: { GIT_LFS_SKIP_SMUDGE: '1' } },
    require(name) { return modules[name]; },
  });
  return { api: module.exports, calls };
}
const ASSET = Buffer.from('main asset');
const POINTER = Buffer.from('version https://git-lfs.github.com/spec/v1\n' +
  `oid sha256:${crypto.createHash('sha256').update(ASSET).digest('hex')}\nsize ${ASSET.length}\n`);

test('LFS hydration checks content against the pinned pointer', function() {
  const fixture = CreateLFSFixture(ASSET);
  assert.deepEqual(fixture.api.RestoreLFSFile(POINTER, 'resources/image.png'), ASSET);
  assert.equal(fixture.calls.length, 1);
  assert.equal(fixture.calls[0].options.env.GIT_LFS_SKIP_SMUDGE, '0');
  assert.deepEqual(fixture.calls[0].options.input, POINTER);
});
for (const [name, data] of [['smudge skipped', POINTER], ['wrong content', Buffer.from('other data')], ['wrong size', Buffer.alloc(0)]]) {
  test(`LFS hydration rejects ${name}`, function() {
    const fixture = CreateLFSFixture(data);
    assert.throws(function() { fixture.api.RestoreLFSFile(POINTER, 'resources/image.png'); });
  });
}
test('invalid LFS pointers are rejected before spawning Git', function() {
  const fixture = CreateLFSFixture(ASSET);
  assert.throws(function() { fixture.api.RestoreLFSFile(Buffer.from('invalid'), 'resources/image.png'); });
  assert.equal(fixture.calls.length, 0);
});

test('canonical vendor entry preserves the isolated source configuration', function() {
  const cmake = fs.readFileSync(path.join(__dirname, 'CMakeLists.txt'), 'utf8');
  const canonicalize = cmake.match(/get_filename_component\(VENDOR_TOOLS_DIR [^\n]+ REALPATH\)/);
  assert.ok(canonicalize, 'Vendor entry must be canonicalized');
  assert.ok(canonicalize.index > cmake.indexOf('include(${TGFX_SOURCE_DIR}/third_party/vendor_tools/vendor.cmake)'));
  assert.ok(canonicalize.index < cmake.indexOf('add_subdirectory(${TGFX_SOURCE_DIR} ./tgfx)'));
  const tools = fs.realpathSync(path.resolve(__dirname, '../../third_party/vendor_tools'));
  const entry = path.join(tools, 'vendor-build');
  const source = fs.readFileSync(entry, 'utf8');
  const temporary = fs.mkdtempSync(path.join(__dirname, 'build-vendor-path-'));
  try {
    const link = path.join(temporary, 'linked-tools');
    fs.symlinkSync(tools, link, 'dir');
    const script = path.join(temporary, 'resolve.cmake');
    const resolvedFile = path.join(temporary, 'resolved-path');
    fs.writeFileSync(script, `${canonicalize[0]}\nfile(WRITE "${resolvedFile}" "\${VENDOR_TOOLS_DIR}")\n`);
    execFileSync('cmake', [`-DVENDOR_TOOLS_DIR=${link}`, '-P', script]);
    const resolved = fs.readFileSync(resolvedFile, 'utf8');
    assert.equal(resolved, tools);
    function ResolveConfiguration(invokedEntry) {
      let config;
      const modules = {
        path,
        './lib/CommandLine': { parse() { return { targets: [] }; } },
        './lib/Platform': { Create() { return {}; } },
        './lib/Vendor': class {
          constructor(file) { config = file; }
          buildAll() {}
        },
      };
      const scriptBody = source.replace(/^#![^\n]*\n/, '');
      vm.runInNewContext(`(function() { ${scriptBody} })()`, {
        __filename: entry,
        process: { argv: ['node', invokedEntry], env: {}, cwd() { return temporary; } },
        require(name) { return modules[name]; },
      });
      return config;
    }
    assert.equal(ResolveConfiguration(path.join(link, 'vendor-build')), path.join(link, 'vendor.json'));
    assert.equal(ResolveConfiguration(path.join(resolved, 'vendor-build')), path.join(temporary, 'vendor.json'));
  } finally {
    fs.rmSync(temporary, { recursive: true, force: true });
  }
});
