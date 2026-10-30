// Copyright (C) 2026 Tencent. All rights reserved.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');

const PROJECT_ROOT = path.resolve(__dirname, '../..');

function RestoreLFSFile(pointer, filename) {
  const match = pointer.toString().match(
    /^version https:\/\/git-lfs.github.com\/spec\/v1\noid sha256:([0-9a-f]{64})\nsize (\d+)\n?$/);
  if (!match) throw new Error(`Invalid LFS pointer: ${filename}`);
  const size = Number(match[2]);
  if (!Number.isSafeInteger(size) || size < 0 || size > 0x7fffffff) {
    throw new Error(`Invalid LFS size: ${filename}`);
  }
  const data = execFileSync('git', ['lfs', 'smudge', '--', filename], {
    cwd: PROJECT_ROOT,
    input: pointer,
    maxBuffer: Math.max(size + 1024, 4096),
    env: { ...process.env, GIT_LFS_SKIP_SMUDGE: '0' },
  });
  if (data.length !== size || crypto.createHash('sha256').update(data).digest('hex') !== match[1]) {
    throw new Error(`LFS content does not match the pinned source: ${filename}`);
  }
  return data;
}

function HydrateSource(directory) {
  const source = fs.realpathSync(directory);
  if (!['webgl', 'webgpu'].some(function(backend) {
    return source.startsWith(path.join(__dirname, `build-reference-${backend}`) + path.sep);
  })) {
    throw new Error('LFS hydration requires an isolated reference source directory');
  }
  function Visit(current) {
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const file = path.join(current, entry.name);
      if (entry.isDirectory()) {
        Visit(file);
      } else if (entry.isFile() && fs.statSync(file).size < 1024) {
        const data = fs.readFileSync(file);
        if (data.toString().startsWith('version https://git-lfs.github.com/spec/')) {
          fs.writeFileSync(file, RestoreLFSFile(data, path.relative(source, file)));
        }
      }
    }
  }
  Visit(source);
}

module.exports = { RestoreLFSFile, HydrateSource };

if (require.main === module) {
  HydrateSource(process.argv[2]);
}
