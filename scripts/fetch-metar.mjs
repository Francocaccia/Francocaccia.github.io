// Fetches current METARs from aviationweather.gov and writes src/data/weather.json.
// Run by the "Weather snapshot" workflow every 3 hours, or by hand: npm run fetch:metar
//
// The file is only rewritten when a report actually changed, so the repository does not
// fill up with empty commits. A station missing from the response keeps its previous entry.
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const file = join(root, 'src', 'data', 'weather.json');
const stations = Object.keys(JSON.parse(readFileSync(join(root, 'src', 'data', 'site.json'), 'utf8')).weather);

const previous = existsSync(file) ? JSON.parse(readFileSync(file, 'utf8')) : { stations: {} };

const url = `https://aviationweather.gov/api/data/metar?ids=${stations.join(',')}&format=json`;
const response = await fetch(url, {
  headers: { Accept: 'application/json', 'User-Agent': 'francocaccia-portfolio-snapshot' },
  signal: AbortSignal.timeout(30000),
});
if (!response.ok) {
  console.error(`aviationweather.gov answered HTTP ${response.status}`);
  process.exit(1);
}
const reports = await response.json();
if (!Array.isArray(reports)) {
  console.error('Unexpected response shape (not an array).');
  process.exit(1);
}

const next = { ...previous.stations };
let changed = false;
for (const icao of stations) {
  const r = reports.find((x) => x?.icaoId === icao);
  const valid =
    r && typeof r.rawOb === 'string' && r.rawOb.includes(icao) && Number.isFinite(r.obsTime) && r.obsTime > 0;
  if (!valid) {
    console.warn(`${icao}: no usable report in the response, keeping the previous one.`);
    continue;
  }
  const entry = { raw: r.rawOb.trim(), observedAt: new Date(r.obsTime * 1000).toISOString() };
  if (previous.stations?.[icao]?.raw !== entry.raw || previous.stations?.[icao]?.observedAt !== entry.observedAt) {
    changed = true;
  }
  next[icao] = entry;
  console.log(`${icao}: ${entry.observedAt}  ${entry.raw}`);
}

if (!changed) {
  console.log('No new reports. Snapshot left as it is.');
  process.exit(0);
}

const snapshot = {
  generatedAt: new Date().toISOString(),
  source: 'aviationweather.gov',
  stations: Object.fromEntries(Object.entries(next).sort(([a], [b]) => a.localeCompare(b))),
};
writeFileSync(file, JSON.stringify(snapshot, null, 2) + '\n');
console.log(`Wrote ${file}`);
