const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const puppeteer = require('puppeteer');
const AdmZip = require('adm-zip');

function ReadOption(name, fallback) {
  const index = process.argv.findIndex(function(arg) {
    return arg === name || arg.startsWith(name + '=');
  });
  if (index < 0) return fallback;
  const arg = process.argv[index];
  return arg.includes('=') ? arg.slice(name.length + 1) : process.argv[index + 1];
}

const PROJECT_ROOT = path.resolve(__dirname, '../..');
const WEB_TEST_DIR = fs.realpathSync(path.resolve(ReadOption('--web-root', __dirname)));
const DOWNLOADS_DIR = path.join(WEB_TEST_DIR, '.downloads');
const PORT = 8082;
const filterArgs = process.argv.slice(2).filter(a => a.startsWith('--filter'));
if (filterArgs.length > 1 || (filterArgs.length === 1 &&
    (!filterArgs[0].startsWith('--filter=') || !filterArgs[0].slice('--filter='.length).trim()))) {
  console.error('Provide one nonempty test filter using --filter=PATTERN.');
  process.exit(1);
}
const TEST_FILTER = filterArgs.length === 1 ? filterArgs[0].slice('--filter='.length) : '';
const TIMEOUT_MS = 10 * 60 * 1000;
const BACKEND = ReadOption('--backend', 'webgl');
const MODE = ReadOption('--mode', 'verify');
const REFERENCE_COMMIT = ReadOption('--reference-commit', '');
const TEST_URL = `http://127.0.0.1:${PORT}/index.html?mode=${encodeURIComponent(MODE)}` +
  `&backend=${encodeURIComponent(BACKEND)}&commit=${encodeURIComponent(REFERENCE_COMMIT)}` +
  (TEST_FILTER ? `&filter=${encodeURIComponent(TEST_FILTER)}` : '');
if (MODE === 'reference' && filterArgs.length !== 0) {
  console.error('Reference generation must run the complete test suite without a filter.');
  process.exit(1);
}
if (!['verify', 'reference'].includes(MODE)) {
  console.error(`Invalid mode: "${MODE}". Must be one of: verify, reference`);
  process.exit(1);
}
const ALLOWED_BACKENDS = new Set(['webgl', 'webgpu']);
if (!ALLOWED_BACKENDS.has(BACKEND)) {
  console.error(`Invalid backend: "${BACKEND}". Must be one of: ${[...ALLOWED_BACKENDS].join(', ')}`);
  process.exit(1);
}
// The isolated runtime directory is named after the backend, so a misspelled backend has to be
// rejected above or this check reports a missing commit and directory instead.
if (MODE === 'reference' && (!/^[0-9a-f]{40}$/.test(REFERENCE_COMMIT) ||
    !WEB_TEST_DIR.startsWith(path.join(__dirname, `build-reference-${BACKEND}`) + path.sep))) {
  console.error('Reference generation requires a pinned main commit and isolated runtime directory.');
  process.exit(1);
}

const OUTPUT_ROOT = MODE === 'reference' ? path.join(WEB_TEST_DIR, 'output') : path.join(PROJECT_ROOT, 'test');
const ZIP_DESTINATIONS = {
  'baseline-out.zip': path.join(OUTPUT_ROOT, 'baseline-out'),
  'test-out.zip': path.join(OUTPUT_ROOT, 'out'),
};

const HTTP_SERVER_BIN = path.join(__dirname, 'node_modules', 'http-server', 'bin', 'http-server');

function startHttpServer() {
  return new Promise((resolve, reject) => {
    // Spawn the server directly instead of through npx. npx does not forward SIGTERM to its
    // child, so killing it left an orphan holding the port and broke the next run in the same job.
    const server = spawn(process.execPath, [HTTP_SERVER_BIN, '-p', String(PORT), '--cors', '-c-1'], {
      cwd: WEB_TEST_DIR,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let started = false;
    // A rejected start never reaches the stopHttpServer call in the caller's finally block, so the
    // child has to be reaped here or it keeps the port and the next run fails with EADDRINUSE.
    const fail = (error) => {
      server.kill('SIGTERM');
      reject(error);
    };
    const onData = (data) => {
      const text = data.toString();
      if (!started && text.includes('Hit CTRL-C to stop')) {
        started = true;
        resolve(server);
      }
    };
    server.stdout.on('data', onData);
    server.stderr.on('data', onData);
    server.on('error', (error) => {
      if (!started) fail(error);
    });
    setTimeout(() => {
      if (!started) {
        fail(new Error('http-server failed to start within 10 seconds'));
      }
    }, 10000);
  });
}

function stopHttpServer(server) {
  return new Promise(function(resolve) {
    if (server.exitCode != null || server.signalCode != null) {
      resolve();
      return;
    }
    server.once('exit', function() { resolve(); });
    server.kill('SIGTERM');
  });
}

function extractZip(zipPath, destDir) {
  if (!fs.existsSync(zipPath)) {
    return;
  }
  fs.mkdirSync(destDir, { recursive: true });
  const zip = new AdmZip(zipPath);
  const resolved = path.resolve(destDir);
  for (const entry of zip.getEntries()) {
    const target = path.resolve(destDir, entry.entryName);
    if (!target.startsWith(resolved + path.sep) && target !== resolved) {
      throw new Error(`Zip-Slip path traversal detected: ${entry.entryName}`);
    }
  }
  zip.extractAllTo(destDir, true);
}

async function run() {
  // Clean and create downloads directory.
  if (fs.existsSync(DOWNLOADS_DIR)) {
    fs.rmSync(DOWNLOADS_DIR, { recursive: true });
  }
  fs.mkdirSync(DOWNLOADS_DIR, { recursive: true });

  // Start http-server.
  console.log('Starting http-server...');
  const server = await startHttpServer();
  console.log(`http-server running on port ${PORT}`);

  let browser = null;
  let testExitCode = 1;
  let runtimeFailed = false;
  let configurationValid = false;
  let startedTests = 0;
  let completedTests = 0;
  let passedTests = 0;
  let skippedTests = false;
  const expectedDownloads = new Set();

  try {
    const launchArgs = [
      '--no-sandbox',
      '--disable-setuid-sandbox',
      '--enable-unsafe-webgpu',
      '--enable-features=Vulkan,UseSkiaRenderer,WebGPU',
      '--experimental-wasm-stack-switching',
    ];
    if (process.platform === 'darwin') {
      launchArgs.push('--use-gl=angle', '--use-angle=metal');
    }
    if (process.platform === 'linux') {
      launchArgs.push('--disable-dev-shm-usage');
    }
    // CI runners have no GPU; TGFX_WEBGPU_SWIFTSHADER=1 forces the SwiftShader WebGPU adapter.
    if (BACKEND === 'webgpu' && process.env.TGFX_WEBGPU_SWIFTSHADER &&
        process.env.TGFX_WEBGPU_SWIFTSHADER !== '0') {
      launchArgs.push('--use-webgpu-adapter=swiftshader');
    }
    browser = await puppeteer.launch({
      headless: true,
      args: launchArgs,
    });

    const page = await browser.newPage();

    // Configure download behavior via CDP.
    const client = await page.createCDPSession();
    await client.send('Browser.setDownloadBehavior', {
      behavior: 'allow',
      downloadPath: DOWNLOADS_DIR,
      eventsEnabled: true,
    });

    const pendingDownloads = new Set();
    client.on('Browser.downloadWillBegin', function(event) {
      pendingDownloads.add(event.guid);
    });
    client.on('Browser.downloadProgress', function(event) {
      if (event.state === 'completed' || event.state === 'canceled') {
        pendingDownloads.delete(event.guid);
      }
      if (event.state === 'canceled') {
        runtimeFailed = true;
        console.error(`Download canceled: ${event.guid}`);
      }
    });

    // Promise that resolves when "All tests finished" is logged.
    let resolveTestDone = null;
    const testDone = new Promise((resolve) => {
      resolveTestDone = resolve;
    });

    // Promise that resolves once index.html logs 'Test page actions finished' after all downloads.
    let resolvePageDone = null;
    const pageDone = new Promise(function(resolve) {
      resolvePageDone = resolve;
    });

    page.on('console', function(msg) {
      const text = msg.text();
      console.log(text);
      if (text.startsWith('Test configuration: ')) {
        try {
          const configuration = JSON.parse(text.slice('Test configuration: '.length));
          configurationValid = configuration.mode === MODE && configuration.backend === BACKEND &&
            (MODE !== 'reference' || configuration.commit === REFERENCE_COMMIT);
          if (!configurationValid) runtimeFailed = true;
        } catch (error) {
          runtimeFailed = true;
        }
      }
      const startMatch = text.match(/^\[==========\] Running (\d+) tests? from /);
      const completeMatch = text.match(/^\[==========\] (\d+) tests? from .* ran\./);
      const passedMatch = text.match(/^\[\s*PASSED\s*\] (\d+) tests?\./);
      if (startMatch) startedTests = Number(startMatch[1]);
      if (completeMatch) completedTests = Number(completeMatch[1]);
      if (passedMatch) passedTests = Number(passedMatch[1]);
      if (/^\[\s*SKIPPED\s*\]/.test(text)) skippedTests = true;
      if (/\[WebGPU\] Uncaptured .* error:/.test(text)) {
        runtimeFailed = true;
      }
      const exitMatch = text.match(/All tests finished with exit code:\s*(\d+)/);
      if (exitMatch) {
        testExitCode = parseInt(exitMatch[1], 10);
        runtimeFailed = runtimeFailed || testExitCode !== 0;
        resolveTestDone();
      }
      const downloadMatch = text.match(/^Download started: (.+)$/);
      if (downloadMatch) {
        expectedDownloads.add(downloadMatch[1]);
      }
      if (text === 'Test page actions finished') {
        resolvePageDone();
      }
    });

    page.on('pageerror', function(err) {
      runtimeFailed = true;
      console.error('PAGE ERROR:', err.message);
      resolveTestDone();
      resolvePageDone();
    });

    console.log(`Opening ${TEST_URL}...`);
    await page.goto(TEST_URL, { timeout: TIMEOUT_MS });

    // Wait for tests to complete.
    const testTimeout = new Promise((_, reject) => {
      setTimeout(() => reject(new Error('Test timed out')), TIMEOUT_MS);
    });
    await Promise.race([testDone, testTimeout]);

    // Wait for the page to finish initiating all downloads.
    const pageTimeout = new Promise((_, reject) => {
      setTimeout(() => reject(new Error('Page download actions timed out')), 60000);
    });
    await Promise.race([pageDone, pageTimeout]);

    const downloadStart = Date.now();
    while (pendingDownloads.size > 0 && Date.now() - downloadStart < 60000) {
      await new Promise(function(resolve) { setTimeout(resolve, 500); });
    }
    if (pendingDownloads.size > 0) {
      throw new Error('Downloads did not complete within 60 seconds');
    }
    // Puppeteer reports a download as finished before the file handle is released, so give the CDP
    // download stream a moment to close before the archives are extracted.
    await new Promise(function(resolve) { setTimeout(resolve, 1000); });

  } catch (err) {
    runtimeFailed = true;
    console.error('Test run failed:', err.message);
  } finally {
    if (browser) {
      await browser.close();
    }
    await stopHttpServer(server);
  }

  console.log('\nExtracting downloaded files...');
  for (const [zipName, destDir] of Object.entries(ZIP_DESTINATIONS)) {
    const zipPath = path.join(DOWNLOADS_DIR, zipName);
    if (fs.existsSync(zipPath)) {
      console.log(`  ${zipName} -> ${path.relative(PROJECT_ROOT, destDir)}/`);
      extractZip(zipPath, destDir);
    } else if (expectedDownloads.has(zipName)) {
      runtimeFailed = true;
      console.error(`  Expected download ${zipName} was not received.`);
    } else {
      console.log(`  ${zipName} not generated (no output files).`);
    }
  }

  if (!configurationValid) {
    runtimeFailed = true;
    console.error('Missing or mismatched test artifact configuration. Rebuild the test artifacts.');
  }
  if (MODE === 'reference') {
    if (startedTests === 0 || startedTests !== completedTests || completedTests !== passedTests || skippedTests) {
      runtimeFailed = true;
      console.error('Reference generation did not pass the complete test suite without skips.');
    }
    const zipPath = path.join(DOWNLOADS_DIR, 'reference-cache.zip');
    if (!expectedDownloads.has('reference-cache.zip') || !fs.existsSync(zipPath)) {
      runtimeFailed = true;
      console.error('Complete reference cache archive was not received.');
    }
    if (!runtimeFailed && testExitCode === 0) {
      const { PublishReference } = require('./reference_cache');
      PublishReference(zipPath, BACKEND, REFERENCE_COMMIT, completedTests);
    } else {
      console.error('Reference cache was not changed.');
    }
  }

  // Clean up downloads directory.
  if (fs.existsSync(DOWNLOADS_DIR)) {
    fs.rmSync(DOWNLOADS_DIR, { recursive: true });
  }

  const exitCode = runtimeFailed ? 1 : testExitCode;
  console.log(`\nTest exit code: ${exitCode}`);
  process.exit(exitCode);
}

run().catch((err) => {
  console.error('Fatal error:', err);
  process.exit(1);
});
