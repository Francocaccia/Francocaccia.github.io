// Finance view, browser side (Figure 1). Runs after the page has loaded and never blocks rendering.
//  - When the bot's history feed is live, fetches it (the free host can take about a minute to wake, so the
//    request has a long timeout) and hands it to the C++ core, which keeps only the whitelisted fields and
//    returns validated markup. If anything fails, the labeled illustrative chart stays.
//  - Adds day-by-day inspection: click, touch, hover, the arrow buttons, or the arrow keys. One day is always
//    selected; the sentence under the chart, the numbers row and the markers on the chart all follow it.
import { afterLoad } from '../lib/after-load';
import type { ChartPoint, ChartResult } from '../lib/charts';
import { SIZES, type Variant } from '../lib/charts';

type View = {
  host: HTMLElement;
  points: ChartPoint[];
  plot: ChartResult['plot'];
  exPlot: ChartResult['exPlot'] | null;
};

function start(root: HTMLElement) {
  const { apiBase = '', historyPath = '', historyLive = 'false' } = root.dataset;
  const timeoutMs = Number(root.dataset.timeout) || 75000;
  const fig = root.querySelector<HTMLElement>('[data-fig]');
  const chart = root.querySelector<HTMLElement>('[data-fcz]');
  if (!fig || !chart) return;

  const notice = fig.querySelector<HTMLElement>('[data-chart-notice]')!;
  const noticeText = fig.querySelector<HTMLElement>('[data-notice-text]')!;
  const tableHost = fig.querySelector<HTMLElement>('[data-table-host]');
  const toggle = fig.querySelector<HTMLButtonElement>('[data-mix-toggle]')!;

  const inspector = createInspector(fig, chart);
  inspector.useViews(readViews(chart));
  inspector.enable();

  // 60/40 line on or off. The buttons say what pressing them does, and aria-pressed carries the state.
  toggle.disabled = false;
  toggle.addEventListener('click', () => {
    const on = toggle.getAttribute('aria-pressed') !== 'true';
    toggle.setAttribute('aria-pressed', String(on)); // the label stays fixed; aria-pressed carries the state
    fig.dataset.mix = on ? 'on' : 'off';
  });
  fig.dataset.ready = ''; // reveals the controls, which are hidden without JavaScript

  const setNotice = (state: 'loading' | 'warming' | 'live', text: string) => {
    notice.dataset.state = state;
    noticeText.textContent = text;
  };

  const ILLUSTRATIVE = 'Illustrative data, not real results.';

  async function loadHistory() {
    // Until the bot publishes its history feed, do not request it: that would only log a failed request.
    if (historyLive !== 'true') {
      return setNotice('warming', `${ILLUSTRATIVE} The bot's history feed is not published yet, so this chart shows a made-up series that starts at 100.`);
    }
    await afterLoad();
    setNotice('loading', `${ILLUSTRATIVE} Waking up the bot: the free host can take about a minute to answer.`);
    const warming = () =>
      setNotice('warming', `${ILLUSTRATIVE} Live data is warming up: the history feed has no data yet or could not be reached.`);
    try {
      const response = await fetch(`${apiBase}${historyPath}`, {
        headers: { Accept: 'application/json' },
        signal: AbortSignal.timeout(timeoutMs),
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const raw: unknown = await response.json();
      if (!Array.isArray(raw) || raw.length < 2) return warming();
      const { coreCall } = await import('../lib/core');
      const variants = Object.keys(SIZES) as Variant[];
      const common = { series: raw, title: 'AutoTradingBot vs SPY and 60/40, indexed to 100' };
      const results = await Promise.all(variants.map((v) => coreCall<ChartResult>('chart.live', { ...common, ...SIZES[v] })));
      const views: View[] = variants.map((v, i) => {
        const host = chart!.querySelector<HTMLElement>(`[data-variant="${v}"]`)!;
        // The markup comes from the C++ core, which validated the feed and escaped every string.
        host.innerHTML = results[i].svg + (results[i].excess ?? '');
        return { host, points: results[i].points, plot: results[i].plot, exPlot: results[i].exPlot ?? null };
      });
      inspector.useViews(views);
      const wide = results[results.length - 1];
      if (tableHost) tableHost.innerHTML = wide.table;
      setNotice('live', `Live data from the paper account: ${wide.n} trading days since ${wide.points[0]?.label ?? ''}.`);
      fig!.dataset.live = 'true';
    } catch {
      warming();
    }
  }

  void loadHistory();
}

function readViews(chart: HTMLElement): View[] {
  return [...chart.querySelectorAll<HTMLElement>('.fcz-view')].map((host) => ({
    host,
    points: JSON.parse(host.dataset.points ?? '[]'),
    plot: JSON.parse(host.dataset.plot ?? '{}'),
    exPlot: JSON.parse(host.dataset.explot ?? 'null'),
  }));
}

// ---- inspection layer -------------------------------------------------------------------------

const SVG_NS = 'http://www.w3.org/2000/svg';
const fmt = (n: number) => n.toFixed(2);
const signed = (n: number, suffix = '') => `${n < 0 ? '−' : n > 0 ? '+' : ''}${Math.abs(n).toFixed(2)}${suffix}`;

function createInspector(fig: HTMLElement, chart: HTMLElement) {
  const readout = fig.querySelector<HTMLElement>('[data-readout]')!;
  const fields = fig.querySelectorAll<HTMLElement>('[data-f]');
  const steps = fig.querySelectorAll<HTMLButtonElement>('[data-step]');
  let views: View[] = [];
  let index = -1;
  let pending: number | undefined;

  const first = () => views[0];
  const visible = () => views.find((v) => v.host.offsetParent !== null) ?? first();

  function mark(view: View, i: number) {
    const p = view.points[i];
    const candles = view.host.querySelector<SVGSVGElement>('.fcz-svg:not(.fcz-ex-svg)');
    const strip = view.host.querySelector<SVGSVGElement>('.fcz-ex-svg');
    for (const [svg, y0, y1] of [
      [candles, view.plot.y0, view.plot.y1],
      [strip, 0, view.exPlot?.h ?? 0],
    ] as const) {
      if (!svg || !p) continue;
      svg.querySelector('.fcz-cross')?.remove();
      const cross = document.createElementNS(SVG_NS, 'line');
      cross.setAttribute('class', 'fcz-cross');
      cross.setAttribute('x1', String(p.x));
      cross.setAttribute('x2', String(p.x));
      cross.setAttribute('y1', String(y0));
      cross.setAttribute('y2', String(y1));
      // Behind the candles and bars, in front of the gridlines.
      svg.insertBefore(cross, svg.querySelector('.fcz-wick, .fcz-ex'));
    }
    candles?.querySelectorAll('.fcz-body').forEach((el, k) => el.classList.toggle('sel', k === i));
    strip?.querySelectorAll('.fcz-ex').forEach((el, k) => el.classList.toggle('sel', k === i));
  }

  function render(i: number) {
    if (pending !== undefined) {
      clearTimeout(pending); // a newer choice beats a hover that has not settled yet
      pending = undefined;
    }
    const base = first();
    if (!base || !base.points.length) return;
    index = Math.max(0, Math.min(base.points.length - 1, i));
    views.forEach((v) => mark(v, index));
    const p = base.points[index];
    // Everything goes in with textContent: dates come from a remote feed.
    readout.textContent = p.readout;
    const values: Record<string, string> = {
      o: fmt(p.o),
      h: fmt(p.h),
      l: fmt(p.l),
      c: fmt(p.c),
      chg: signed(p.chg, '%'),
      spy: fmt(p.spy),
      ex: signed(p.ex),
    };
    fields.forEach((el) => (el.textContent = values[el.dataset.f ?? ''] ?? ''));
    steps.forEach((b) => (b.disabled = index + Number(b.dataset.step) < 0 || index + Number(b.dataset.step) > base.points.length - 1));
  }

  // Hovering sweeps the days quickly: the sentence follows the pointer but is not spoken (keys, buttons and clicks are).
  function select(i: number, settle = false) {
    if (!settle) return render(i);
    if (pending !== undefined) clearTimeout(pending);
    const base = first();
    const target = Math.max(0, Math.min((base?.points.length ?? 1) - 1, i));
    const p = base?.points[target];
    if (!p) return;
    views.forEach((v) => mark(v, target));
    index = target;
    // Live announcements are paused while the hover sentence updates, so a sweep is not read out day by day.
    readout.setAttribute('aria-live', 'off');
    pending = window.setTimeout(() => {
      render(target);
      window.setTimeout(() => readout.setAttribute('aria-live', 'polite'), 300);
    }, 140);
  }

  function nearest(event: PointerEvent): number | null {
    const view = visible();
    const svg = view?.host.querySelector<SVGSVGElement>('.fcz-svg:not(.fcz-ex-svg)');
    if (!view || !svg) return null;
    const box = svg.getBoundingClientRect();
    if (!box.width) return null;
    const x = ((event.clientX - box.left) / box.width) * view.plot.w;
    let best = 0;
    view.points.forEach((p, i) => {
      if (Math.abs(p.x - x) < Math.abs(view.points[best].x - x)) best = i;
    });
    return best;
  }

  chart.addEventListener('pointerdown', (event) => {
    const i = nearest(event);
    if (i === null) return;
    if (event.pointerType !== 'mouse') chart.setPointerCapture(event.pointerId);
    select(i);
  });
  chart.addEventListener('pointermove', (event) => {
    if (event.pointerType === 'mouse') {
      const i = nearest(event);
      if (i !== null) select(i, true);
    } else if (chart.hasPointerCapture(event.pointerId)) {
      const i = nearest(event);
      if (i !== null) select(i, true);
    }
  });
  chart.addEventListener('keydown', (event) => {
    const last = (first()?.points.length ?? 1) - 1;
    const moves: Record<string, number> = { ArrowLeft: index - 1, ArrowRight: index + 1, Home: 0, End: last };
    if (event.key in moves) {
      event.preventDefault();
      select(moves[event.key]);
    }
  });
  steps.forEach((b) => b.addEventListener('click', () => select(index + Number(b.dataset.step))));

  return {
    useViews(next: View[]) {
      views = next;
      render(next[0] ? next[0].points.length - 1 : 0);
    },
    enable() {
      steps.forEach((b) => (b.disabled = false));
      render(index);
    },
  };
}

// Last, so every constant above exists before the first render.
const root = document.querySelector<HTMLElement>('[data-finance]');
if (root) start(root);
