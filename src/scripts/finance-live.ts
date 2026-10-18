// Finance view, browser side. Runs after the page has loaded and never blocks rendering.
//  - Fetches the bot's public /stats and /stats/history (the free host can take about a
//    minute to wake, so each request has a long timeout).
//  - Hands the payloads to the C++ core, which keeps only the whitelisted percentage fields
//    and returns validated markup. If anything fails, the static illustrative chart stays.
//  - Adds the hover / keyboard inspection layer to whichever chart is on screen.
import type { ChartResult } from '../lib/charts';

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
  hasData: boolean;
}

const root = document.querySelector<HTMLElement>('[data-finance]');
if (root) start(root);

function start(root: HTMLElement) {
  const { apiBase = '', statsPath = '', historyPath = '', trackingSince = '', historyLive = 'false' } = root.dataset;
  const timeoutMs = Number(root.dataset.timeout) || 75000;

  const tilesNote = root.querySelector<HTMLElement>('[data-tiles-note]');
  const notice = root.querySelector<HTMLElement>('[data-chart-notice]');
  const noticeText = root.querySelector<HTMLElement>('[data-notice-text]');
  const foot = root.querySelector<HTMLElement>('[data-chart-foot]');
  const tableHost = root.querySelector<HTMLElement>('[data-table-host]');
  const chart = root.querySelector<HTMLElement>('[data-fcz]');
  if (!chart) return;

  const inspector = createInspector(chart);
  inspector.useViews(readStaticViews(chart));

  let corePromise: Promise<typeof import('../lib/core')> | undefined;
  const core = () => (corePromise ??= import('../lib/core'));

  const getJson = async (path: string): Promise<unknown> => {
    const response = await fetch(`${apiBase}${path}`, {
      headers: { Accept: 'application/json' },
      signal: AbortSignal.timeout(timeoutMs),
    });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
  };

  const setNotice = (state: 'loading' | 'warming' | 'live', text: string) => {
    if (notice) notice.dataset.state = state;
    if (noticeText) noticeText.textContent = text;
  };

  async function loadStats() {
    try {
      const raw = await getJson(statsPath);
      const { coreCall } = await core();
      const stats = await coreCall<StatsResult>('stats.normalize', { stats: raw, trackingSince });
      for (const tile of stats.tiles) {
        const el = root!.querySelector<HTMLElement>(`[data-tile="${tile.id}"]`);
        if (!el) continue;
        el.dataset.tone = tile.tone;
        el.querySelector('[data-value]')!.textContent = tile.text;
        el.querySelector('[data-caption]')!.textContent = tile.caption;
      }
      const parts = [stats.note, stats.asOf ? `Updated ${stats.asOf}.` : ''].filter(Boolean);
      if (tilesNote) tilesNote.textContent = parts.join(' ') || `Tracking since ${stats.startDate}.`;
    } catch {
      if (tilesNote) {
        tilesNote.textContent = `Live stats did not answer. Tiles show a dash until they do (tracking since ${trackingSince}).`;
      }
    }
  }

  async function loadHistory() {
    // Until the bot publishes its history feed, do not request it: that would only log a failed request.
    if (historyLive !== 'true') {
      return setNotice(
        'warming',
        'Illustrative data shown, not real results. Live data warming up: the bot has not published its history feed yet.',
      );
    }
    setNotice(
      'loading',
      'Illustrative data shown, not real results. Checking for live data: the free host can take about a minute to wake.',
    );
    const warming = () =>
      setNotice(
        'warming',
        'Illustrative data shown, not real results. Live data warming up: the history feed has no data yet or is not reachable.',
      );
    try {
      const raw = await getJson(historyPath);
      if (!Array.isArray(raw) || raw.length < 2) return warming();
      const { coreCall } = await core();
      const common = { series: raw, title: 'AutoTradingBot vs SPY and 60/40, indexed to 100' };
      const [wide, narrow] = await Promise.all([
        coreCall<ChartResult>('chart.live', { ...common, width: 720, height: 360, idPrefix: 'fcz-w' }),
        coreCall<ChartResult>('chart.live', { ...common, width: 360, height: 300, idPrefix: 'fcz-n' }),
      ]);
      const views: View[] = [
        { host: chart!.querySelector<HTMLElement>('[data-variant="wide"]')!, result: wide },
        { host: chart!.querySelector<HTMLElement>('[data-variant="narrow"]')!, result: narrow },
      ];
      // The markup comes from the C++ core, which validated the feed and escaped every string.
      views[0].host.innerHTML = wide.svg;
      views[1].host.innerHTML = narrow.svg;
      inspector.useViews(views);
      if (tableHost) tableHost.innerHTML = wide.table;
      const first = wide.points[0]?.label ?? '';
      setNotice('live', `Live data from the paper account, ${wide.n} trading days since ${first}.`);
      if (foot) foot.textContent = 'Indexed to 100 at the start. Hover or focus the chart for daily values.';
    } catch {
      warming();
    }
  }

  void loadStats();
  void loadHistory();
}

// ---- chart inspection layer -------------------------------------------------------------

type View = { host: HTMLElement; result: Pick<ChartResult, 'points' | 'plot'> };

function readStaticViews(chart: HTMLElement): View[] {
  return [...chart.querySelectorAll<HTMLElement>('.fcz-view')].map((host) => ({
    host,
    result: { points: JSON.parse(host.dataset.points ?? '[]'), plot: JSON.parse(host.dataset.plot ?? '{}') },
  }));
}

function createInspector(chart: HTMLElement) {
  const tip = chart.querySelector<HTMLElement>('[data-tip]')!;
  const live = chart.querySelector<HTMLElement>('[data-fcz-live]')!;
  let views: View[] = [];
  let index = -1;

  const visible = () => views.find((v) => v.host.offsetParent !== null);

  const fmt = (n: number) => n.toFixed(2);

  function row(key: string, value: string, name: string) {
    const line = document.createElement('div');
    line.className = 'tip-row';
    const k = document.createElement('i');
    k.className = `tip-key ${key}`;
    const strong = document.createElement('strong');
    strong.textContent = value;
    const label = document.createElement('span');
    label.textContent = name;
    line.append(k, strong, label);
    return line;
  }

  function show(i: number, announce: boolean) {
    const view = visible();
    if (!view) return;
    const pts = view.result.points;
    if (!pts.length) return;
    index = Math.max(0, Math.min(pts.length - 1, i));
    const p = pts[index];
    const svg = view.host.querySelector<SVGSVGElement>('svg');
    if (!svg) return;

    svg.querySelector('.fcz-cross')?.remove();
    const cross = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    cross.setAttribute('class', 'fcz-cross');
    cross.setAttribute('x1', String(p.x));
    cross.setAttribute('x2', String(p.x));
    cross.setAttribute('y1', String(view.result.plot.y0));
    cross.setAttribute('y2', String(view.result.plot.y1));
    svg.insertBefore(cross, svg.querySelector('.fcz-wick'));

    // Values lead, names follow. Everything goes in with textContent: labels come from a remote feed.
    const date = document.createElement('div');
    date.className = 'tip-date';
    date.textContent = p.label;
    tip.replaceChildren(
      date,
      row(p.c >= p.o ? 'up' : 'dn', fmt(p.c), `Bot close (${p.c >= p.o ? 'up' : 'down'} day)`),
      row('', `${fmt(p.o)} / ${fmt(p.h)} / ${fmt(p.l)}`, 'open / high / low'),
      row('spy', fmt(p.spy), 'SPY'),
      row('mix', fmt(p.mix), '60/40'),
    );
    tip.hidden = false;

    const box = svg.getBoundingClientRect();
    const host = chart.getBoundingClientRect();
    const px = (p.x / view.result.plot.w) * box.width + (box.left - host.left);
    const room = host.width - px;
    tip.style.left = room > tip.offsetWidth + 16 ? `${px + 12}px` : `${Math.max(0, px - tip.offsetWidth - 12)}px`;

    if (announce) {
      live.textContent = `${p.label}: bot closed ${fmt(p.c)}, SPY ${fmt(p.spy)}, 60/40 ${fmt(p.mix)}.`;
    }
  }

  function hide() {
    tip.hidden = true;
    index = -1;
    chart.querySelectorAll('.fcz-cross').forEach((el) => el.remove());
  }

  chart.addEventListener('pointermove', (event) => {
    const view = visible();
    const svg = view?.host.querySelector('svg');
    if (!view || !svg) return;
    const box = svg.getBoundingClientRect();
    const x = ((event.clientX - box.left) / box.width) * view.result.plot.w;
    let best = 0;
    view.result.points.forEach((p, i) => {
      if (Math.abs(p.x - x) < Math.abs(view.result.points[best].x - x)) best = i;
    });
    show(best, false);
  });
  chart.addEventListener('pointerleave', () => {
    if (document.activeElement !== chart) hide();
  });
  chart.addEventListener('focus', () => show(visible() ? visible()!.result.points.length - 1 : 0, true));
  chart.addEventListener('blur', hide);
  chart.addEventListener('keydown', (event) => {
    const last = (visible()?.result.points.length ?? 1) - 1;
    const moves: Record<string, number> = {
      ArrowLeft: index - 1,
      ArrowRight: index + 1,
      Home: 0,
      End: last,
    };
    if (event.key in moves) {
      event.preventDefault();
      show(moves[event.key], true);
    } else if (event.key === 'Escape') {
      hide();
    }
  });

  return {
    useViews(next: View[]) {
      views = next;
      hide();
    },
  };
}
