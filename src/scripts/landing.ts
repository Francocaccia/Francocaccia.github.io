// Landing page enhancements, all optional: the page is complete without this file. No WebAssembly, no
// requests, no scroll listeners.
//  1. A scene starts playing once its chapter scrolls into view (where scroll-driven animations are missing).
//  2. The "Pause motion" switch (WCAG 2.2.2), remembered in localStorage when it is available.
//  3. The health-factor slider, which is plain arithmetic.

const root = document.documentElement;

// 1. Play-once reveal.
const targets = document.querySelectorAll<HTMLElement>('.chapter, .statement');
if ('IntersectionObserver' in window) {
  const seen = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        entry.target.classList.add('in');
        seen.unobserve(entry.target);
      }
    },
    { threshold: 0.15 },
  );
  targets.forEach((el) => seen.observe(el));
} else {
  targets.forEach((el) => el.classList.add('in'));
}

// 2. Pause motion.
const toggle = document.querySelector<HTMLButtonElement>('[data-motion-toggle]');
if (toggle) {
  const label = toggle.querySelector<HTMLElement>('.mt-label')!;
  const paint = () => {
    const off = root.dataset.motion === 'off';
    toggle.setAttribute('aria-pressed', String(off));
    label.textContent = off ? 'Resume motion' : 'Pause motion';
  };
  toggle.hidden = false;
  paint();
  toggle.addEventListener('click', () => {
    const off = root.dataset.motion !== 'off';
    if (off) root.dataset.motion = 'off';
    else delete root.dataset.motion;
    try {
      localStorage.setItem('fc-motion', off ? 'off' : 'on');
    } catch {
      /* storage blocked: the choice lasts for this visit only */
    }
    paint();
  });
}

// 3. Health factor = collateral x liquidation threshold / debt, with illustrative numbers.
const hf = document.querySelector<HTMLElement>('[data-hf]');
const slider = hf?.querySelector<HTMLInputElement>('input[type="range"]');
if (hf && slider) {
  const COLLATERAL = 150;
  const THRESHOLD = 0.8;
  const DEBT = 100;
  const move = hf.querySelector<HTMLElement>('[data-hf-move]')!;
  const value = hf.querySelector<HTMLElement>('[data-hf-value]')!;
  const state = hf.querySelector<HTMLElement>('[data-hf-state]')!;
  const fill = hf.querySelector<HTMLElement>('.hf-fill')!;
  const update = () => {
    const pct = Number(slider.value);
    const factor = (COLLATERAL * (1 + pct / 100) * THRESHOLD) / DEBT;
    move.textContent = `${pct > 0 ? '+' : pct < 0 ? '−' : ''}${Math.abs(pct)}%`;
    value.textContent = factor.toFixed(2);
    const healthy = factor >= 1;
    state.textContent = healthy ? 'healthy' : 'could be liquidated';
    hf.dataset.state = healthy ? 'healthy' : 'liquidatable';
    fill.style.setProperty('--hf', String(factor));
  };
  slider.disabled = false;
  slider.addEventListener('input', update);
  update();
}

// 4. "Ask me anything": picks one of the pre-written answers (already in the page as a list) by keywords. It makes
// nothing up; if no keyword matches it says so and points at the terminal view, which can search everything.
const ask = document.querySelector<HTMLElement>('[data-ask]');
if (ask) {
  const form = ask.querySelector<HTMLFormElement>('[data-ask-form]')!;
  const input = ask.querySelector<HTMLInputElement>('[data-ask-input]')!;
  const out = ask.querySelector<HTMLElement>('[data-ask-out]')!;
  const items = [...ask.querySelectorAll<HTMLElement>('[data-ask-a]')];
  const chips = [...ask.querySelectorAll<HTMLButtonElement>('[data-ask-q]')];
  const words = (text: string) => text.toLowerCase().match(/[a-z0-9+#]+/g) ?? [];

  const show = (item: HTMLElement | null) => {
    chips.forEach((c) => c.setAttribute('aria-pressed', String(c.dataset.askQ === item?.dataset.askA)));
    if (!item) {
      const p = document.createElement('p');
      p.textContent = 'I only have written answers for the questions above. Try one of them, or open the terminal view, which can search everything.';
      out.replaceChildren(p);
      return;
    }
    const title = document.createElement('h3');
    title.textContent = item.querySelector('summary')!.textContent;
    out.replaceChildren(title, ...[...item.querySelector('.ask-a')!.cloneNode(true).childNodes]);
  };

  const find = (text: string): HTMLElement | null => {
    const typed = words(text);
    let best: HTMLElement | null = null;
    let top = 0;
    for (const item of items) {
      const keys = words(`${item.dataset.keys} ${item.querySelector('summary')!.textContent}`);
      const score = typed.filter((w) => w.length > 1 && keys.some((k) => k === w || (w.length > 3 && k.startsWith(w)))).length;
      if (score > top) {
        top = score;
        best = item;
      }
    }
    return best;
  };

  ask.dataset.ready = '';
  chips.forEach((chip) => chip.addEventListener('click', () => show(items.find((i) => i.dataset.askA === chip.dataset.askQ) ?? null)));
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (input.value.trim()) show(find(input.value));
  });
}
