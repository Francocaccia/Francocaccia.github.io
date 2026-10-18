// Checks every link in dist/: internal pages, assets and #anchors always; external URLs with --external.
// Usage: node scripts/check-links.mjs [--external]
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, extname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'dist');
const external = process.argv.includes('--external');
const SITE = 'https://francocaccia.github.io';

const walk = (dir) =>
  readdirSync(dir).flatMap((name) => {
    const path = join(dir, name);
    return statSync(path).isDirectory() ? walk(path) : [path];
  });

const pages = walk(root).filter((f) => f.endsWith('.html'));
const idsOf = new Map();
const idsFor = (file) => {
  if (!idsOf.has(file)) {
    const html = readFileSync(file, 'utf8');
    idsOf.set(file, new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1])));
  }
  return idsOf.get(file);
};

const problems = [];
const externals = new Map(); // url -> pages using it
let checked = 0;

function fileFor(urlPath) {
  const clean = decodeURIComponent(urlPath.split('?')[0]);
  const target = join(root, clean);
  if (existsSync(target) && statSync(target).isFile()) return target;
  const index = join(target, 'index.html');
  return existsSync(index) ? index : null;
}

for (const page of pages) {
  const html = readFileSync(page, 'utf8');
  const pageUrl = '/' + relative(root, dirname(page)).replaceAll('\\', '/');
  const links = [...html.matchAll(/\s(?:href|src)="([^"]*)"/g)].map((m) => m[1]);
  for (const raw of links) {
    if (!raw || raw.startsWith('mailto:') || raw.startsWith('tel:') || raw.startsWith('data:') || raw.startsWith('javascript:')) continue;
    checked++;
    if (/^https?:\/\//.test(raw) && !raw.startsWith(SITE)) {
      externals.set(raw, [...(externals.get(raw) ?? []), pageUrl]);
      continue;
    }
    const url = new URL(raw.startsWith(SITE) ? raw.slice(SITE.length) || '/' : raw, `http://x${pageUrl === '/' ? '/' : pageUrl + '/'}`);
    const file = url.pathname === '/' && !url.pathname.slice(1) ? fileFor('/') : fileFor(url.pathname);
    if (!file) {
      problems.push(`${pageUrl}: missing target ${raw}`);
      continue;
    }
    if (url.hash && extname(file) === '.html' && !idsFor(file).has(decodeURIComponent(url.hash.slice(1)))) {
      problems.push(`${pageUrl}: missing anchor ${raw}`);
    }
  }
}

console.log(`Checked ${checked} links in ${pages.length} pages (${externals.size} distinct external URLs).`);

if (external) {
  const skipHosts = ['www.linkedin.com']; // LinkedIn answers bots with HTTP 999
  for (const [url, from] of externals) {
    if (skipHosts.includes(new URL(url).hostname)) {
      console.log(`  skipped  ${url} (blocks automated requests)`);
      continue;
    }
    try {
      let res = await fetch(url, { method: 'HEAD', redirect: 'follow', signal: AbortSignal.timeout(20000), headers: { 'User-Agent': 'link-check' } });
      if (res.status === 405 || res.status === 403) res = await fetch(url, { redirect: 'follow', signal: AbortSignal.timeout(20000), headers: { 'User-Agent': 'link-check' } });
      console.log(`  ${res.ok ? 'ok     ' : 'FAIL   '}  ${res.status}  ${url}`);
      if (!res.ok) problems.push(`${from[0]}: ${url} answered HTTP ${res.status}`);
    } catch (error) {
      console.log(`  FAIL     ${url} (${error.message})`);
      problems.push(`${from[0]}: ${url} unreachable (${error.message})`);
    }
  }
}

if (problems.length) {
  console.error(`\n${problems.length} problem(s):`);
  for (const p of problems) console.error('  ' + p);
  process.exit(1);
}
console.log('No broken links.');
