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
