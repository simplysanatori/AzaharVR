#!/usr/bin/env node
// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Pictures of Pit's arm with the app's own shaders, in a headless browser:
//
//   node tools/kiu_hand/preview/shots.mjs --dump DUMP_DIR --shots SHOTS.json --out OUT_DIR
//        --kit DIR_WITH_node_modules [--chrome /usr/bin/chromium]
//
// DUMP_DIR is what tools/kiu_hand/model_dump.cpp wrote. SHOTS.json: {"size": 768,
// "background": [r, g, b], "shots": [{"name", "look": "shader" | "clay", "eye", "target", "up",
// "fov", "draws": [{"verts": "fist.verts", "model": [16 numbers, column major], "mirrored":
// false, "plate": true}]}]}. Each shot is written as OUT_DIR/<name>.png. The kit folder needs
// puppeteer-core (nothing is fetched from the network; the page is served from 127.0.0.1).
import fs from 'node:fs';
import http from 'node:http';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
for (const k of ['dump', 'shots', 'out', 'kit']) {
  if (!args[k]) {
    console.error('usage: shots.mjs --dump DIR --shots FILE.json --out DIR --kit DIR [--chrome PATH]');
    process.exit(2);
  }
}
const VIS = path.resolve(HERE, '../../../src/frame_xr/vis');
const grab = (file, name) => {
  const m = new RegExp(`${name} = R"\\(([\\s\\S]*?)\\)";`).exec(fs.readFileSync(path.join(VIS, file), 'utf8'));
  if (!m) throw new Error(`${name} not found in ${file}`);
  return m[1];
};
const SHADERS = { '/shader/vert': grab('model_shaders.h', 'kModelVert'), '/shader/frag': grab('pit_hand_shader.h', 'kPitHandFrag') };
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json' };

const server = http.createServer((req, res) => {
  const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (SHADERS[url]) return res.writeHead(200, { 'content-type': 'text/plain' }).end(SHADERS[url]);
  const root = url.startsWith('/dump/') ? path.resolve(args.dump) : HERE;
  const file = path.resolve(root, url.replace(/^\/dump\//, '').replace(/^\//, ''));
  if (!file.startsWith(root + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) return res.writeHead(404).end();
  res.writeHead(200, { 'content-type': TYPES[path.extname(file)] ?? 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const require = createRequire(path.join(path.resolve(args.kit), 'package.json'));
const puppeteer = require('puppeteer-core');
const profile = fs.mkdtempSync(path.join(fs.realpathSync('/tmp'), 'kiu-hand-shots-'));
const browser = await puppeteer.launch({
  executablePath: args.chrome ?? '/usr/bin/chromium', headless: true, userDataDir: profile,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--no-first-run', '--disable-extensions'],
});
const pid = browser.process()?.pid;
let failed = false;
try {
  const page = await browser.newPage();
  page.on('console', (m) => console.error('page:', m.text()));
  page.on('pageerror', (e) => console.error('page error:', e.message));
  await page.goto(`http://127.0.0.1:${server.address().port}/page.html`);
  await page.waitForFunction('window.ready === true', { timeout: 30000 });
  const spec = JSON.parse(fs.readFileSync(args.shots, 'utf8'));
  fs.mkdirSync(args.out, { recursive: true });
  // a few shots at a time, so no one call carries too many pictures
  for (let i = 0; i < spec.shots.length; i += 8) {
    const part = { ...spec, shots: spec.shots.slice(i, i + 8) };
    const done = await page.evaluate((s) => window.renderShots(s), part);
    for (const d of done) fs.writeFileSync(path.join(args.out, `${d.name}.png`), Buffer.from(d.png.split(',')[1], 'base64'));
  }
  console.log(`${spec.shots.length} shots in ${args.out}`);
} catch (e) {
  console.error(String(e));
  failed = true;
} finally {
  await browser.close().catch(() => {});
  if (pid) {
    try { process.kill(pid, 0); process.kill(pid); } catch { /* already gone */ }
  }
  fs.rmSync(profile, { recursive: true, force: true });
  server.close();
}
process.exit(failed ? 1 : 0);
