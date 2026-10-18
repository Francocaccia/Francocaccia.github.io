// Which METAR to show for a station: the latest snapshot written by the "Weather snapshot"
// workflow if there is one, otherwise the labeled sample in the data file.
import snapshot from '../data/weather.json';
import { site } from './site';

export const STALE_MS = 6 * 60 * 60 * 1000;

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

export function ageText(ms: number): string {
  const minutes = Math.max(0, Math.round(ms / 60000));
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  if (hours < 48) return `${hours} h ${minutes % 60} min`;
  return `${Math.floor(hours / 24)} days`;
}
