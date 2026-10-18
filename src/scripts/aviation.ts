// Aviation view, browser side: live report ages, group tooltips, the paste-a-report box and the
// crosswind calculator. The decoding itself is the C++ core (WebAssembly); this file wires the DOM.
// Markup the core returns is built from escaped text, so it is assigned with innerHTML.

import { afterLoad } from '../lib/after-load';

const STALE_MS = 6 * 60 * 60 * 1000;

const root = document.querySelector<HTMLElement>('[data-av]');
if (root) start(root);

function start(root: HTMLElement) {
  ageCounters();
  groupTooltips();
  rovingGroups(document);
  void interactive(root);
}

// ---- report ages --------------------------------------------------------------------------

function ageText(ms: number): string {
  const minutes = Math.max(0, Math.round(ms / 60000));
  if (minutes < 60) return `${minutes} min`;
  const hours = Math.floor(minutes / 60);
  if (hours < 48) return `${hours} h ${minutes % 60} min`;
  return `${Math.floor(hours / 24)} days`;
}

function ageCounters() {
  const update = () => {
    document.querySelectorAll<HTMLElement>('[data-observed]').forEach((el) => {
      const age = Date.now() - Date.parse(el.dataset.observed ?? '');
      if (Number.isNaN(age)) return;
      const stale = age > STALE_MS;
      el.dataset.state = stale ? 'stale' : 'fresh';
      const text = el.querySelector('[data-age]');
      if (text) text.textContent = stale ? `${ageText(age)} old now. Older than 6 hours.` : `${ageText(age)} old now.`;
      const flag = el.querySelector<HTMLElement>('[data-stale-flag]');
      if (flag) flag.hidden = !stale;
    });
  };
  update();
  setInterval(update, 60000);
}

// ---- one tooltip for every decoded group ---------------------------------------------------
// Positioned in the viewport and clamped to it, so a group at the screen edge never causes a
// horizontal scroll. The decoded table next to each report holds the same information.

function groupTooltips() {
  const tip = document.createElement('div');
  tip.className = 'av-tip';
  tip.setAttribute('role', 'tooltip');
  tip.hidden = true;
  document.body.append(tip);

  const show = (el: HTMLElement) => {
    const text = el.dataset.tip;
    if (!text) return;
    tip.textContent = text;
    tip.hidden = false;
    const box = el.getBoundingClientRect();
    const width = tip.offsetWidth;
    const left = Math.max(8, Math.min(box.left, innerWidth - width - 8));
    const above = box.top - tip.offsetHeight - 8;
    tip.style.left = `${left}px`;
    tip.style.top = `${above >= 8 ? above : box.bottom + 8}px`;
  };
  const hide = () => {
    tip.hidden = true;
  };
  const target = (event: Event) => (event.target as Element | null)?.closest<HTMLElement>('.mt');

  document.addEventListener('pointerover', (e) => {
    const el = target(e);
    if (el) show(el);
  });
  document.addEventListener('pointerout', (e) => {
    if (target(e)) hide();
  });
  document.addEventListener('focusin', (e) => {
    const el = target(e);
    if (el) show(el);
  });
  document.addEventListener('focusout', (e) => {
    if (target(e)) hide();
  });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') hide();
  });
  // Scrolling hides a hover tooltip, but a focused group keeps its tooltip and follows it.
  addEventListener(
    'scroll',
    () => {
      const active = document.activeElement as HTMLElement | null;
      if (active?.classList.contains('mt')) show(active);
      else hide();
    },
    { passive: true },
  );
}

// ---- one Tab stop per report line ---------------------------------------------------------------
// A decoded report has a dozen groups. Instead of a dozen Tab stops each, the first group of a line is
// the stop and the arrow keys move along the line (a "roving tabindex"). Tab always moves on.

function rovingGroups(scope: ParentNode) {
  scope.querySelectorAll<HTMLElement>('.mx-raw').forEach((line) => {
    const groups = [...line.querySelectorAll<HTMLElement>('.mt')];
    if (!groups.length) return;
    groups.forEach((g, i) => (g.tabIndex = i === 0 ? 0 : -1));
    if (line.dataset.roving) return;
    line.dataset.roving = 'true';
    line.addEventListener('keydown', (event) => {
      const current = (event.target as Element).closest<HTMLElement>('.mt');
      if (!current) return;
      const list = [...line.querySelectorAll<HTMLElement>('.mt')];
      const i = list.indexOf(current);
      const next = { ArrowRight: i + 1, ArrowLeft: i - 1, Home: 0, End: list.length - 1 }[event.key];
      if (next === undefined) return;
      event.preventDefault();
      const target = list[Math.max(0, Math.min(list.length - 1, next))];
      list.forEach((g) => (g.tabIndex = g === target ? 0 : -1));
      target.focus();
    });
  });
}

// ---- tools that need the C++ core -----------------------------------------------------------

type Wind = { present: boolean; calm: boolean; variable: boolean; dir: number | null; speedKt: number | null; gustKt: number | null };

async function interactive(root: HTMLElement) {
  await afterLoad();
  const { coreCall } = await import('../lib/core');
  const winds: Record<string, Wind> = JSON.parse(root.dataset.winds ?? '{}');
  const examples: { label: string; raw: string }[] = JSON.parse(root.dataset.examples ?? '[]');

  // ------------------------------------------------------------------ paste a METAR or TAF
  const form = root.querySelector<HTMLFormElement>('[data-paste-form]')!;
  const input = root.querySelector<HTMLTextAreaElement>('[data-paste-input]')!;
  const out = root.querySelector<HTMLElement>('[data-paste-out]')!;
  const go = root.querySelector<HTMLButtonElement>('[data-paste-go]')!;
  const clear = root.querySelector<HTMLButtonElement>('[data-paste-clear]')!;
  const chips = [...root.querySelectorAll<HTMLButtonElement>('[data-example]')];

  const message = (text: string, kind = 'muted') => {
    const p = document.createElement('p');
    p.className = kind;
    p.textContent = text;
    out.replaceChildren(p);
  };

  type Decoded = { ok: boolean; error?: string; html: string; timeline: string };

  async function decode() {
    const raw = input.value.trim();
    if (!raw) return message('Paste a METAR or a TAF first, or pick an example.');
    try {
      if (/^TAF(\s|$)/i.test(raw)) {
        const common = { raw, monthDays: 31 };
        const [wide, narrow] = await Promise.all([
          coreCall<Decoded>('taf.decode', { ...common, width: 720, idPrefix: 'pw' }),
          coreCall<Decoded>('taf.decode', { ...common, width: 360, idPrefix: 'pn' }),
        ]);
        if (!wide.ok) return message(wide.error ?? 'Could not decode that TAF.', 'error');
        out.innerHTML = `<div class="rv timeline"><div class="rv-wide">${wide.timeline}</div><div class="rv-narrow">${narrow.timeline}</div></div>${wide.html}`;
        rovingGroups(out);
        return;
      }
      const result = await coreCall<Decoded>('metar.decode', { raw });
      if (!result.ok) return message(result.error ?? 'Could not decode that METAR.', 'error');
      out.innerHTML = result.html;
      rovingGroups(out);
    } catch (error) {
      message((error as Error).message, 'error');
    }
  }

  form.addEventListener('submit', (event) => {
    event.preventDefault();
    void decode();
  });
  clear.addEventListener('click', () => {
    input.value = '';
    message('Paste a report above, or pick an example.');
    input.focus();
  });
  chips.forEach((chip) =>
    chip.addEventListener('click', () => {
      input.value = examples[Number(chip.dataset.example)]?.raw ?? '';
      void decode();
    }),
  );
  [input, go, clear, ...chips].forEach((el) => (el.disabled = false));
  message('Paste a report above, or pick an example.');

  // ------------------------------------------------------------------ crosswind calculator
  const source = root.querySelector<HTMLSelectElement>('[data-xw-source]')!;
  const field = (name: string) => root.querySelector<HTMLInputElement>(`[data-xw="${name}"]`)!;
  const dirEl = field('dir'), speedEl = field('speed'), gustEl = field('gust'), rwyEl = field('runway'), varEl = field('variation');
  const xwOut = root.querySelector<HTMLElement>('[data-xw-out]')!;
  const num = (el: HTMLInputElement) => (el.value.trim() === '' ? null : Number(el.value));

  function fillFromMetar() {
    const wind = winds[source.value];
    if (!wind) return;
    if (!wind.present || wind.speedKt === null) {
      dirEl.value = '';
      speedEl.value = '';
      gustEl.value = '';
      return;
    }
    // Calm wind: 0 kt from any direction. Variable direction: the user must choose one.
    dirEl.value = wind.calm ? '0' : wind.dir === null ? '' : String(wind.dir);
    speedEl.value = String(wind.speedKt);
    gustEl.value = wind.gustKt === null ? '' : String(wind.gustKt);
  }

  const xwMessage = (text: string, kind = 'muted') => {
    const p = document.createElement('p');
    p.className = kind;
    p.textContent = text;
    xwOut.replaceChildren(p);
  };

  async function calculate() {
    const runway = rwyEl.value.trim();
    if (!runway) return xwMessage('Enter a runway number to see the components.');
    const dir = num(dirEl), speed = num(speedEl);
    if (dir === null || speed === null) {
      return xwMessage(
        source.value !== 'manual' && winds[source.value]?.variable
          ? 'That METAR reports a variable wind direction. Enter a direction to try.'
          : 'Enter the wind direction and speed.',
      );
    }
    const gust = num(gustEl);
    try {
      const r = await coreCall<{
        headText: string; crossText: string; gustHeadText?: string; gustCrossText?: string;
        windMagnetic: number; runwayHeading: number; error?: string; svg: string;
      }>('wind.components', { dir, speed, gust: gust ?? undefined, runway, variation: num(varEl) ?? 0, idPrefix: 'xw' });
      if (r.error) return xwMessage(r.error, 'error');
      const lines = [
        `Runway ${runway.toUpperCase()}, heading ${r.runwayHeading}° magnetic. Wind ${r.windMagnetic}° magnetic.`,
        `${r.headText}. ${r.crossText}.`,
      ];
      if (r.gustHeadText) lines.push(`In the gusts: ${r.gustHeadText.toLowerCase()}, ${r.gustCrossText?.toLowerCase()}.`);
      const text = document.createElement('div');
      text.className = 'xw-text';
      lines.forEach((l, i) => {
        const p = document.createElement('p');
        p.className = i === 1 ? 'xw-big' : '';
        p.textContent = l;
        text.append(p);
      });
      const legend = document.createElement('p');
      legend.className = 'muted xw-key';
      legend.textContent = 'Diagram: blue arrow is the wind, green is the head or tail component, amber is the crosswind.';
      text.append(legend);
      const figure = document.createElement('div');
      figure.className = 'xw-fig';
      figure.innerHTML = r.svg; // markup from the C++ core
      xwOut.replaceChildren(text, figure);
    } catch (error) {
      xwMessage((error as Error).message, 'error');
    }
  }

  source.addEventListener('change', () => {
    if (source.value !== 'manual') fillFromMetar();
    void calculate();
  });
  [dirEl, speedEl, gustEl, rwyEl, varEl].forEach((el) =>
    el.addEventListener('input', () => {
      if (el !== rwyEl && el !== varEl) source.value = 'manual';
      void calculate();
    }),
  );
  root.querySelector<HTMLFormElement>('[data-xw-form]')!.addEventListener('submit', (e) => e.preventDefault());
  fillFromMetar();
  xwMessage('Enter a runway number to see the components.');
  [source, dirEl, speedEl, gustEl, rwyEl, varEl].forEach((el) => (el.disabled = false));
}
