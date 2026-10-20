// How old a weather report is, and when it counts as stale. Pure, so the build (src/lib/weather.ts) and the
// browser (src/scripts/aviation.ts) use the same rule without the browser pulling in the site data.

export const STALE_MS = 6 * 60 * 60 * 1000;

export function ageText(ms: number): string {
  const minutes = Math.max(0, Math.round(ms / 60000));
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  if (hours < 48) return `${hours} h ${minutes % 60} min`;
  return `${Math.floor(hours / 24)} days`;
}
