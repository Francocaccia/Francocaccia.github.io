// Splits a raw METAR into display tokens, each with a class for its kind (station, time, wind, ...).
// Used for the previews on the landing page; the Aviation page uses the full C++ decoder instead.

export type MetarKind = 'stn' | 'time' | 'wind' | 'vis' | 'cloud' | 'temp' | 'qnh' | 'rmk';

export function metarKind(token: string): MetarKind {
  if (/^[A-Z]{4}$/.test(token)) return 'stn';
  if (/^\d{6}Z$/.test(token)) return 'time';
  if (/KT$/.test(token)) return 'wind';
  if (/^(\d{4}|\d+SM)$/.test(token)) return 'vis';
  if (/^(FEW|SCT|BKN|OVC)/.test(token)) return 'cloud';
  if (/^M?\d{2}\/M?\d{2}$/.test(token)) return 'temp';
  if (/^[QA]\d{4}$/.test(token)) return 'qnh';
  return 'rmk';
}

/** The report up to its remarks (RMK), or its first `max` groups if it has none. */
export function metarTokens(raw: string, max = 9): { text: string; kind: MetarKind }[] {
  const all = raw.split(' ').filter((t) => t && t !== 'METAR');
  const cut = all.includes('RMK') ? all.indexOf('RMK') : max;
  return all.slice(0, cut).map((text) => ({ text, kind: metarKind(text) }));
}
