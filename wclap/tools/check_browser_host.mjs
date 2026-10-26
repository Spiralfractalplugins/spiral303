// Loads the Spiral303 WCLAP bundle in the WebCLAP browser-test-host and
// checks that it loads, its interface paints the canvas and nothing breaks.
// The interface lives inside an iframe (the host's page proxy), so the
// canvases are looked for in every frame.
//   node check_browser_host.mjs [module-url-path] [screenshot]
//
// Expects the WebCLAP browser-test-host served on :8137 with our bundle in
// its spiral303/ folder, and playwright-core installed (it downloads no
// browsers of its own -- the chromium of an existing playwright install is
// used, or CHROMIUM_PATH points at one):
//
//   cd /tmp && git clone --depth 1 --recurse-submodules \
//       https://github.com/WebCLAP/browser-test-host
//   (cd /tmp/browser-test-host && python3 server.py 8137 &)
//   node wclap/tools/check_browser_host.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { chromium } from 'playwright-core';

const modulePath = process.argv[2] || '/spiral303/Spiral303-wclap.tar.gz';
const screenshotPath = process.argv[3] || '/tmp/spiral303_browser_testhost.png';
const base = 'http://127.0.0.1:8137';

// The playwright cache holds one directory per chromium revision; the newest
// revision that actually has a launchable binary wins.
const findChromium = () => {
    const cache = path.join (os.homedir(),
        process.platform === 'darwin' ? 'Library/Caches/ms-playwright'
                                      : '.cache/ms-playwright');
    if (!fs.existsSync (cache))
        return undefined;

    const revisions = fs.readdirSync (cache)
        .filter (name => /^chromium-\d+$/.test (name))
        .sort ((a, b) => Number (b.split ('-')[1]) - Number (a.split ('-')[1]));

    const layouts = process.platform === 'darwin'
        ? ['chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing',
           'chrome-mac/Chromium.app/Contents/MacOS/Chromium']
        : ['chrome-linux/chrome'];

    for (const revision of revisions)
        for (const layout of layouts) {
            const binary = path.join (cache, revision, layout);
            if (fs.existsSync (binary))
                return binary;
        }
    return undefined;
};

const exe = process.env.CHROMIUM_PATH ?? findChromium ();
if (exe === undefined) {
    console.error ('no chromium found: install playwright browsers or set CHROMIUM_PATH');
    process.exit (1);
}

const failed = [];
const pass = (what) => console.log ('PASS ' + what);
const fail = (what) => { console.log ('FAIL ' + what); failed.push (what); };

const browser = await chromium.launch ({
  executablePath: exe, headless: true,
  args: ['--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage ({ viewport: { width: 1500, height: 1000 } });
const errors = [];
const notFound = [];
page.on ('pageerror', (e) => errors.push ('pageerror: ' + e.message));
page.on ('console', (m) => {
  if (m.type() === 'error' && ! /favicon/.test (m.text()))
    errors.push ('console: ' + m.text());
});
page.on ('response', (r) => { if (r.status() === 404) notFound.push (r.url()); });

await page.goto (base + '/?module=' + modulePath);

try {
  await page.waitForFunction (() => document.title.includes ('Spiral303'), undefined, { timeout: 30000 });
  pass ('the plugin loads: descriptor name reached the title');
} catch (e) {
  fail ('the plugin did not load (title: "' + (await page.title()) + '")');
}

await page.waitForTimeout (6000);

// The interface frame: the host puts the plugin page into an iframe.
const frames = page.frames();
console.log ('frames: ' + frames.length);

let paintResult = { painted: false };
for (const frame of frames) {
  try {
    const result = await frame.evaluate (() => {
      for (const canvas of document.querySelectorAll ('canvas')) {
        try {
          const data = canvas.getContext ('2d').getImageData (0, 0, canvas.width, canvas.height).data;
          const seen = new Set();
          for (let i = 0; i < data.length; i += 89)
            seen.add (data[i] + ',' + data[i + 1] + ',' + data[i + 2]);
          if (seen.size > 5)
            return { painted: true, colours: seen.size, size: canvas.width + 'x' + canvas.height };
        } catch (e) { /* a canvas the read refuses */ }
      }
      return { painted: false };
    });
    if (result.painted && ! paintResult.painted) {
      paintResult = result;
      break;
    }
  } catch (e) { /* a frame of another origin: skipped */ }
}

if (paintResult.painted)
  pass ('the interface canvas paints itself (' + paintResult.colours + ' colours, ' + paintResult.size + ')');
else
  fail ('no interface canvas painted itself');

// The state has to have arrived: a page that only drew its empty cabinet would
// pass the paint check with a broken handshake (an unquoted version made the
// state JSON unparseable, and the page swallows the parse error).
let stateResult = { ok: false };
for (const frame of frames) {
  try {
    const result = await frame.evaluate (() => {
      const l = window.__layout;
      return { ok: !!l && l.knobs.length === 18 && !!l.grid && l.gateCells.length === 16,
               preset: (window.__state && window.__state.preset) || '',
               version: (window.__state && window.__state.version) || '' };
    });
    if (result.ok) { stateResult = result; break; }
  } catch (e) { /* a frame of another origin: skipped */ }
}
if (stateResult.ok)
  pass ('the state reached the page (18 knobs, 16 gate cells)');
else
  fail ('the page never got a state it could parse');

// The page (and its frames) must be free of uncaught errors; the nested
// "instead of /module.wasm" notice is the loader's hint, not a failure.
const real = errors.filter ((x) =>
  ! /instead of \/module\.wasm/.test (x) && ! /state_mark_dirty/.test (x) && ! /404 \(File not found\)/.test (x));
if (real.length > 0)
  fail ('uncaught errors:\n  ' + real.join ('\n  '));
else
  pass ('no uncaught errors');
const real404 = notFound.filter ((u) => ! /favicon/.test (u));
if (real404.length > 0)
  console.log ('note: 404s on ' + real404.join (', '));
else
  pass ('no 404 requests');

await page.screenshot ({ path: screenshotPath, fullPage: true });
console.log ('screenshot saved to ' + screenshotPath);

await browser.close();
process.exit (failed.length > 0 ? 1 : 0);
