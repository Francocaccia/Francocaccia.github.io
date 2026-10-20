// Fills the shared performance tiles from the bot's public /stats. Never blocks rendering: it waits
// for the page to finish loading, and the free host can take about a minute to wake, so the request
// has a long timeout. The C++ core keeps only the whitelisted percentage fields.
import { afterLoad } from '../lib/after-load';
import { fetchBotJson } from '../lib/bot-fetch';

interface Tile {
  id: string;
  text: string;
  tone: string;
  caption: string;
}
interface StatsResult {
  tiles: Tile[];
  startDate: string;
  asOf: string;
  note: string;
}

export async function startBotTiles(root: HTMLElement) {
  const { apiBase = '', statsPath = '', trackingSince = '' } = root.dataset;
  const timeoutMs = Number(root.dataset.timeout) || 75000;
  const note = root.querySelector<HTMLElement>('[data-tiles-note]');
  const say = (text: string) => {
    if (note) note.textContent = text;
  };

  await afterLoad();
  say('Waking up the bot. The free host can take about a minute; the placeholders stay until it answers.');
  try {
    const raw = await fetchBotJson(`${apiBase}${statsPath}`, timeoutMs);
    const { coreCall } = await import('../lib/core');
    const stats = await coreCall<StatsResult>('stats.normalize', { stats: raw, trackingSince });
    for (const tile of stats.tiles) {
      const el = root.querySelector<HTMLElement>(`[data-tile="${tile.id}"]`);
      if (!el) continue;
      el.dataset.tone = tile.tone;
      el.querySelector('[data-value]')!.textContent = tile.text;
      el.querySelector('[data-caption]')!.textContent = tile.caption;
    }
    const parts = [stats.note, stats.asOf ? `Updated ${stats.asOf}.` : ''].filter(Boolean);
    say(parts.join(' ') || `Tracking since ${stats.startDate}.`);
  } catch {
    // The values stay as placeholders; the captions stop promising that a feed is about to answer.
    root.querySelectorAll<HTMLElement>('[data-caption]').forEach((el) => (el.textContent = 'no data'));
    say(`The bot did not answer, so the placeholders stay (tracking since ${trackingSince}).`);
  }
}

document.querySelectorAll<HTMLElement>('[data-bot]').forEach((root) => void startBotTiles(root));
