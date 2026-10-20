// Checks the built site (dist/): size budgets and a few things that must hold on every build (the Aviation page
// has one globe and no stale map text), then scans it for the private names, so a name that slipped into any
// page, script or data file fails the build. Terms come from $PRIVATE_TERMS (comma separated) and the untracked
// .private-terms file, as in check-data.mjs. In CI (CI=true) an empty list is an error: the check must
// never pass just because the secret was not set.
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { dirname, join, extname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
// ---- budgets and invariants ---------------------------------------------------------------------------------
const problems = [];
const worldFile = join(root, 'src', 'data', 'world-110m.json');
if (existsSync(worldFile)) {
  const gz = gzipSync(readFileSync(worldFile)).length;
  if (gz > 20 * 1024) problems.push(`world-110m.json is ${gz} bytes gzipped (limit 20 KB)`);
}
const aviation = join(root, 'dist', 'aviation', 'index.html');
if (existsSync(aviation)) {
  const html = readFileSync(aviation, 'utf8');
  const globes = (html.match(/class="globe-svg"/g) ?? []).length;
  if (globes !== 1) problems.push(`aviation page has ${globes} globe SVGs (expected exactly 1)`);
  if (/Coordinates approximate/.test(html)) problems.push('aviation page still says "Coordinates approximate"');
  if (html.length > 90 * 1024) problems.push(`aviation page is ${html.length} bytes (budget 90 KB)`);
}
// The landing page: no WebAssembly and no request to the bot, only the gateway's fonts, a small script budget.
const landing = join(root, 'dist', 'index.html');
if (existsSync(landing)) {
  const html = readFileSync(landing, 'utf8');
  if (/\.wasm/.test(html)) problems.push('the landing page references a .wasm file');
  if (/onrender\.com/.test(html)) problems.push('the landing page mentions the bot API host');
  for (const m of html.matchAll(/rel="preload" as="font" type="font\/woff2" href="([^"]+)"/g)) {
    if (!/ibm-plex/.test(m[1])) problems.push(`the landing page preloads a font that is not IBM Plex: ${m[1]}`);
  }
  let jsBytes = 0;
  const files = new Set([...html.matchAll(/<script[^>]+src="([^"]+\.js)"/g), ...html.matchAll(/<link rel="modulepreload"[^>]+href="([^"]+)"/g)].map((m) => m[1]));
  for (const href of files) {
    const file = join(root, 'dist', href);
    if (existsSync(file)) jsBytes += gzipSync(readFileSync(file)).length;
  }
  // Small scripts are inlined into the page by the build; they count too.
  for (const m of html.matchAll(/<script(?![^>]*(?:src=|type="application\/ld\+json"))[^>]*>([\s\S]*?)<\/script>/g)) jsBytes += gzipSync(m[1]).length;
  if (jsBytes > 5 * 1024) problems.push(`the landing page loads ${jsBytes} bytes of gzipped JavaScript (budget 5 KB)`);
  console.log(`landing: ${html.length} bytes of HTML, ${jsBytes} bytes of gzipped JavaScript`);
}

if (problems.length) {
  for (const p of problems) console.error(`  ERROR  ${p}`);
  process.exit(1);
}

const forbidden = (process.env.PRIVATE_TERMS ?? '').split(',');
const termsFile = join(root, '.private-terms');
if (existsSync(termsFile)) forbidden.push(...readFileSync(termsFile, 'utf8').split(/\r?\n/));
const terms = forbidden.map((t) => t.trim().toLowerCase()).filter(Boolean);

if (!terms.length) {
  const message = 'check-dist: no forbidden-term list found (set the PRIVATE_TERMS secret or create .private-terms).';
  if (process.env.CI) {
    console.error(message);
    process.exit(1);
  }
  console.warn(message);
  process.exit(0);
}

const TEXT = new Set(['.html', '.js', '.mjs', '.css', '.json', '.xml', '.txt', '.svg', '.webmanifest']);
const hits = [];
let files = 0;
(function walk(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) walk(path);
    else if (TEXT.has(extname(name))) {
      files += 1;
      // Accent-insensitive as well as case-insensitive.
      const text = readFileSync(path, 'utf8').toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g, '');
      if (terms.some((t) => text.includes(t.normalize('NFD').replace(/[̀-ͯ]/g, '')))) hits.push(path.slice(root.length + 1));
    }
  }
})(join(root, 'dist'));

if (hits.length) {
  for (const h of hits) console.error(`  ERROR  ${h}: contains a forbidden term`); // never echo the term
  process.exit(1);
}
console.log(`dist: ${files} text file(s) checked against ${terms.length} forbidden term(s), none found`);
