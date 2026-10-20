// Validates src/data/site.json with the C++ core.
// Forbidden terms come from $PRIVATE_TERMS (comma separated) and from an
// untracked .private-terms file (one term per line), so the list itself is
// never committed. Errors fail the run; TODO placeholders only warn.
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import createCore from '../src/generated/core.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const data = JSON.parse(readFileSync(join(root, 'src', 'data', 'site.json'), 'utf8'));

const forbidden = (process.env.PRIVATE_TERMS ?? '').split(',');
const termsFile = join(root, '.private-terms');
if (existsSync(termsFile)) forbidden.push(...readFileSync(termsFile, 'utf8').split(/\r?\n/));
const terms = forbidden.map((t) => t.trim()).filter(Boolean);
if (!terms.length && process.env.CI) {
  // In CI an empty list must not pass silently (the secret was probably not set).
  console.error('check-data: no forbidden-term list found (set the PRIVATE_TERMS secret).');
  process.exit(1);
}

const core = await createCore();
const raw = core.ccall('fc_call', 'number', ['string', 'string'], ['site.check', JSON.stringify({ data, forbidden: terms })]);
const reply = JSON.parse(core.UTF8ToString(raw));
core._fc_free(raw);

if (!reply.ok) {
  console.error(`data check failed to run: ${reply.error}`);
  process.exit(2);
}

const errors = reply.result.filter((i) => i.severity === 'error');
const warnings = reply.result.filter((i) => i.severity === 'warning');
for (const i of warnings) console.warn(`  warn   ${i.path}: ${i.message}`);
for (const i of errors) console.error(`  ERROR  ${i.path}: ${i.message}`);

console.log(
  `site.json: ${errors.length} error(s), ${warnings.length} warning(s)` +
    (terms.length ? `, checked against ${terms.length} forbidden term(s)` : ', no forbidden-term list found'),
);
process.exit(errors.length ? 1 : 0);
