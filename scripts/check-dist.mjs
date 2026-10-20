// Scans the built site (dist/) for the private names, so a name that slipped into any page, script or
// data file fails the build. Terms come from $PRIVATE_TERMS (comma separated) and the untracked
// .private-terms file, as in check-data.mjs. In CI (CI=true) an empty list is an error: the check must
// never pass just because the secret was not set.
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, extname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
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
