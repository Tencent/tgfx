// Copyright (C) 2026 Tencent. All rights reserved.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const AdmZip = require('adm-zip');

const PROJECT_ROOT = path.resolve(__dirname, '../..');

function GetSourceVersion(commit) {
  if (!/^[0-9a-f]{40}$/.test(commit)) {
    throw new Error('A full pinned main commit is required');
  }
  execFileSync('git', ['merge-base', '--is-ancestor', commit, 'origin/main'], { cwd: PROJECT_ROOT });
  return execFileSync('git', ['show', `${commit}:test/baseline/version.json`], { cwd: PROJECT_ROOT });
}

function Hash(data) {
  return crypto.createHash('sha256').update(data).digest('hex');
}

function ValidateReference(md5Data, versionData, sourceVersion) {
  if (!versionData.equals(sourceVersion)) {
    throw new Error('Reference version does not match the pinned main source');
  }
  const versions = JSON.parse(versionData.toString());
  const md5 = JSON.parse(md5Data.toString());
  function CountEntries(node, version, key) {
    if (!node || Array.isArray(node) || typeof node !== 'object' || Object.keys(node).length === 0) {
      throw new Error(`Empty or invalid reference at ${key}`);
    }
    let count = 0;
    for (const [name, value] of Object.entries(node)) {
      if (['__proto__', 'prototype', 'constructor'].includes(name) || name.includes('/') || !name) {
        throw new Error(`Invalid reference key: ${name}`);
      }
      const source = version && Object.hasOwn(version, name) ? version[name] : undefined;
      if (typeof value === 'string') {
        if (!/^[0-9a-f]{32}$/.test(value) || typeof source !== 'string' || source.length === 0) {
          throw new Error(`Missing source version or invalid MD5 at ${key}/${name}`);
        }
        count++;
      } else {
        count += CountEntries(value, source, `${key}/${name}`);
      }
    }
    return count;
  }
  return CountEntries(md5, versions, '');
}

function CacheDirectory(backend) {
  if (!['webgl', 'webgpu'].includes(backend)) {
    throw new Error(`Invalid backend: ${backend}`);
  }
  return path.join(PROJECT_ROOT, 'test/baseline/.cache', backend);
}

function IsReferenceCurrent(backend, commit) {
  try {
    const directory = CacheDirectory(backend);
    const md5 = fs.readFileSync(path.join(directory, 'md5.json'));
    const version = fs.readFileSync(path.join(directory, 'version.json'));
    const receipt = JSON.parse(fs.readFileSync(path.join(directory, 'reference.json'), 'utf8'));
    const entries = ValidateReference(md5, version, GetSourceVersion(commit));
    return receipt.backend === backend && receipt.commit === commit &&
      receipt.md5Hash === Hash(md5) && receipt.versionHash === Hash(version) &&
      receipt.entries === entries && Number.isSafeInteger(receipt.tests) && receipt.tests > 0;
  } catch (error) {
    return false;
  }
}

function PublishReference(zipPath, backend, commit, tests) {
  if (!Number.isSafeInteger(tests) || tests <= 0) {
    throw new Error('Reference generation must run the complete nonempty test suite');
  }
  const destination = CacheDirectory(backend);
  const zip = new AdmZip(zipPath);
  const entries = zip.getEntries();
  const files = new Map();
  for (const entry of entries) {
    const type = (entry.attr >>> 16) & 0xf000;
    if (!['md5.json', 'version.json'].includes(entry.entryName) || entry.isDirectory ||
        files.has(entry.entryName) || (type !== 0 && type !== 0x8000)) {
      throw new Error(`Unexpected reference archive entry: ${entry.entryName}`);
    }
    files.set(entry.entryName, entry.getData());
  }
  if (files.size !== 2) {
    throw new Error('Reference archive must contain both md5.json and version.json');
  }
  const md5 = files.get('md5.json');
  const version = files.get('version.json');
  const count = ValidateReference(md5, version, GetSourceVersion(commit));
  const receipt = { backend, commit, tests, entries: count, md5Hash: Hash(md5), versionHash: Hash(version) };
  const parent = path.dirname(destination);
  fs.mkdirSync(parent, { recursive: true });
  const lock = `${destination}.lock`;
  fs.mkdirSync(lock);
  let staging;
  let backup;
  let moved = false;
  try {
    if (fs.existsSync(destination) && fs.lstatSync(destination).isSymbolicLink()) {
      throw new Error('Reference cache must not be a symbolic link');
    }
    staging = fs.mkdtempSync(path.join(parent, `.reference-${backend}-`));
    fs.writeFileSync(path.join(staging, 'md5.json'), md5);
    fs.writeFileSync(path.join(staging, 'version.json'), version);
    fs.writeFileSync(path.join(staging, 'reference.json'), JSON.stringify(receipt, null, 2) + '\n');
    backup = fs.mkdtempSync(path.join(parent, `.previous-${backend}-`));
    if (fs.existsSync(destination)) {
      fs.renameSync(destination, path.join(backup, 'cache'));
      moved = true;
    }
    try {
      fs.renameSync(staging, destination);
      staging = undefined;
    } catch (error) {
      if (moved) {
        fs.renameSync(path.join(backup, 'cache'), destination);
      }
      throw error;
    }
  } finally {
    if (staging) fs.rmSync(staging, { recursive: true, force: true });
    if (backup && (!moved || fs.existsSync(destination))) {
      fs.rmSync(backup, { recursive: true, force: true });
    }
    fs.rmdirSync(lock);
  }
  console.log(`Published ${count} reference entries for ${backend} from ${commit.slice(0, 9)}.`);
}

module.exports = { ValidateReference, IsReferenceCurrent, PublishReference };

if (require.main === module) {
  if (process.argv[2] !== '--check') {
    console.error('Use --check <backend> <main commit> to check a reference cache.');
    process.exit(1);
  }
  process.exit(IsReferenceCurrent(process.argv[3], process.argv[4]) ? 0 : 1);
}
