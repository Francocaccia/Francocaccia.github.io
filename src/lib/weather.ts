// Which METAR to show for a station: the latest snapshot written by the "Weather snapshot"
// workflow if there is one, otherwise the labeled sample in the data file.
import snapshot from '../data/weather.json';
import { site } from './site';
import { ageText, STALE_MS } from './age';

export { ageText, STALE_MS };

export interface StationWeather {
  icao: string;
  raw: string;
  observedAt: string; // ISO
  sample: boolean;
  source: string;
  snapshotAt: string | null;
}

export function stationWeather(icao: string): StationWeather {
  const snap = (snapshot.stations as Record<string, { raw: string; observedAt: string } | undefined>)[icao];
  if (snap) {
    return { icao, ...snap, sample: false, source: snapshot.source, snapshotAt: snapshot.generatedAt };
  }
  const fallback = (site.weather as Record<string, { raw: string; observedAt: string; sample: boolean }>)[icao];
  return { icao, raw: fallback.raw, observedAt: fallback.observedAt, sample: fallback.sample, source: 'site data', snapshotAt: null };
}

/** "2026-10-18 15:00 UTC" */
export const utcStamp = (iso: string): string => `${iso.slice(0, 10)} ${iso.slice(11, 16)} UTC`;

