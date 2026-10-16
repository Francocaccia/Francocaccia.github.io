// Mode memory and switcher behavior. Everything here is an enhancement:
// the switcher is a <details> element with plain links, and the gateway
// never redirects.
import { STORAGE_KEY } from '../config';

const readMode = (): string | null => {
  try {
    return localStorage.getItem(STORAGE_KEY);
  } catch {
    return null;
  }
};

const writeMode = (mode: string): void => {
  try {
    localStorage.setItem(STORAGE_KEY, mode);
  } catch {
    /* private mode or blocked storage: ignore */
  }
};

// Remember the mode of the page being viewed.
const page = document.documentElement.dataset.page;
if (page === 'aviation' || page === 'terminal' || page === 'finance') writeMode(page);

// Remember the mode chosen from the switcher or the gateway doors.
document.addEventListener('click', (event) => {
  const link = (event.target as Element | null)?.closest<HTMLAnchorElement>('a[data-mode]');
  if (link?.dataset.mode) writeMode(link.dataset.mode);
});

// Gateway: reveal "Continue in <mode>" for the remembered mode only.
const remembered = readMode();
if (remembered) {
  const chip = document.querySelector<HTMLElement>(`[data-continue-for="${CSS.escape(remembered)}"]`);
  if (chip) chip.hidden = false;
}

// Close the switcher on Escape (returning focus to it) and on outside click.
const menu = document.querySelector<HTMLDetailsElement>('.fc-switch details');
if (menu) {
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && menu.open) {
      menu.open = false;
      menu.querySelector('summary')?.focus();
    }
  });
  document.addEventListener('click', (event) => {
    if (menu.open && !menu.contains(event.target as Node)) menu.open = false;
  });
}
