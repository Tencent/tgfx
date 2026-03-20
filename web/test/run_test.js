const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const puppeteer = require('puppeteer');
const AdmZip = require('adm-zip');

const WEB_TEST_DIR = __dirname;
const PROJECT_ROOT = path.resolve(WEB_TEST_DIR, '../..');
const DOWNLOADS_DIR = path.join(WEB_TEST_DIR, '.downloads');
const PORT = 8082;
const TEST_URL = `http://127.0.0.1:${PORT}/index.html`;
const TIMEOUT_MS = 10 * 60 * 1000;

// Map downloaded zip files to their extraction destinations.
const ZIP_DESTINATIONS = {
  'baseline-out.zip': path.join(PROJECT_ROOT, 'test/baseline-out'),
  'test-out.zip': path.join(PROJECT_ROOT, 'test/out'),
  'cache-web.zip': path.join(PROJECT_ROOT, 'test/baseline/.cache-web'),
};

function startHttpServer() {
  return new Promise((resolve, reject) => {
    const server = spawn('npx', ['http-server', '-p', String(PORT), '--cors', '-c-1'], {
      cwd: WEB_TEST_DIR,
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    let started = false;
    const onData = (data) => {
      const text = data.toString();
      if (!started && text.includes('Hit CTRL-C to stop')) {
        started = true;
        resolve(server);
      }
    };
    server.stdout.on('data', onData);
    server.stderr.on('data', onData);
    server.on('error', reject);
    setTimeout(() => {
      if (!started) {
        reject(new Error('http-server failed to start within 10 seconds'));
      }
    }, 10000);
  });
}

function extractZip(zipPath, destDir) {
  if (!fs.existsSync(zipPath)) {
    return;
  }
  fs.mkdirSync(destDir, { recursive: true });
  const zip = new AdmZip(zipPath);
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

  try {
    browser = await puppeteer.launch({
      headless: 'new',
      args: [
        '--no-sandbox',
        '--disable-setuid-sandbox',
        '--use-gl=angle',
        '--use-angle=metal',
        '--enable-webgl2-compute-context',
      ],
    });

    const page = await browser.newPage();

    // Configure download behavior via CDP.
    const client = await page.createCDPSession();
    await client.send('Browser.setDownloadBehavior', {
      behavior: 'allow',
      downloadPath: DOWNLOADS_DIR,
    });

    // Track completed downloads via CDP events.
    let pendingDownloads = 0;
    client.on('Browser.downloadWillBegin', () => {
      pendingDownloads++;
    });
    client.on('Browser.downloadProgress', (event) => {
      if (event.state === 'completed' || event.state === 'canceled') {
        pendingDownloads--;
      }
    });

    // Promise that resolves when "All tests finished" is logged.
    let resolveTestDone = null;
    const testDone = new Promise((resolve) => {
      resolveTestDone = resolve;
    });

    // Promise that resolves when all page-side download actions complete (cache-web.zip is last).
    let resolvePageDone = null;
    const pageDone = new Promise((resolve) => {
      resolvePageDone = resolve;
    });

    // Listen to console output and relay to terminal.
    page.on('console', (msg) => {
      const text = msg.text();
      console.log(text);
      const exitMatch = text.match(/All tests finished with exit code:\s*(\d+)/);
      if (exitMatch) {
        testExitCode = parseInt(exitMatch[1], 10);
        resolveTestDone();
      }
      // cache-web.zip is the last download action in index.html.
      if (text.includes('Download started: cache-web.zip') ||
          text.includes('Failed to create cache-web.zip')) {
        resolvePageDone();
      }
    });

    // Listen to page errors.
    page.on('pageerror', (err) => {
      console.error('PAGE ERROR:', err.message);
    });

    // Navigate to test page. Use a long timeout since WASM loading can take a while.
    console.log(`Opening ${TEST_URL}...`);
    page.goto(TEST_URL, { timeout: TIMEOUT_MS }).catch((err) => {
      console.error('Navigation error:', err.message);
    });

    // Wait for tests to complete.
    const testTimeout = new Promise((_, reject) => {
      setTimeout(() => reject(new Error('Test timed out')), TIMEOUT_MS);
    });
    await Promise.race([testDone, testTimeout]);

    // Wait for the page to finish initiating all downloads.
    const pageTimeout = new Promise((_, reject) => {
      setTimeout(() => reject(new Error('Page download actions timed out')), 60000);
    });
    await Promise.race([pageDone, pageTimeout]).catch((err) => {
      console.warn(err.message);
    });

    // Wait for pending CDP downloads to finish writing to disk.
    const downloadStart = Date.now();
    while (pendingDownloads > 0 && Date.now() - downloadStart < 60000) {
      await new Promise((r) => setTimeout(r, 500));
    }

    // Small delay to ensure all file handles are released.
    await new Promise((r) => setTimeout(r, 1000));

  } catch (err) {
    console.error('Test run failed:', err.message);
  } finally {
    if (browser) {
      await browser.close();
    }
    server.kill('SIGTERM');
  }

  // Extract downloaded zip files to their destinations.
  console.log('\nExtracting downloaded files...');
  for (const [zipName, destDir] of Object.entries(ZIP_DESTINATIONS)) {
    const zipPath = path.join(DOWNLOADS_DIR, zipName);
    if (fs.existsSync(zipPath)) {
      console.log(`  ${zipName} -> ${path.relative(PROJECT_ROOT, destDir)}/`);
      extractZip(zipPath, destDir);
    } else {
      console.warn(`  ${zipName} not found, skipping.`);
    }
  }

  // Clean up downloads directory.
  if (fs.existsSync(DOWNLOADS_DIR)) {
    fs.rmSync(DOWNLOADS_DIR, { recursive: true });
  }

  console.log(`\nTest exit code: ${testExitCode}`);
  process.exit(testExitCode);
}

run().catch((err) => {
  console.error('Fatal error:', err);
  process.exit(1);
});
