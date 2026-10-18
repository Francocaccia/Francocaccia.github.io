// Terminal view, browser side. The shell itself (filesystem, commands, completion) is the
// C++ core running as WebAssembly; this file only handles input, history and the DOM.
import { STORAGE_KEY } from '../config';
import { renderLines, type TermLine } from '../lib/term-render';

interface ExecResult {
  lines: TermLine[];
  actions: { type: string; url?: string; mode?: string }[];
  cwd: string;
}

const root = document.querySelector<HTMLElement>('[data-term]');
if (root) void start(root);

async function start(root: HTMLElement) {
  const screen = root.querySelector<HTMLElement>('[data-screen]')!;
  const form = root.querySelector<HTMLFormElement>('[data-form]')!;
  const input = root.querySelector<HTMLInputElement>('[data-input]')!;
  const prompt = root.querySelector<HTMLElement>('[data-ps]')!;
  const title = root.querySelector<HTMLElement>('[data-title]')!;
  const termBox = root.querySelector<HTMLElement>('.term')!;
  const cheat = root.querySelector<HTMLDetailsElement>('[data-cheat]');
  const {
    apiBase = '',
    statsPath = '',
    trackingSince = '',
    modes = '',
  } = root.dataset;
  const timeoutMs = Number(root.dataset.timeout) || 75000;

  // On phones the cheat sheet starts closed so the terminal stays in view.
  if (cheat && matchMedia('(max-width: 899px)').matches) cheat.open = false;

  const site = JSON.parse(document.getElementById('term-site')!.textContent!);
  const { coreCall } = await import('../lib/core');
  await coreCall('term.init', {
    site,
    modes: modes.split(',').filter(Boolean),
    metars: JSON.parse(root.dataset.metars ?? '{}'),
  });

  let cwd = '~';
  const history: string[] = ['cat about.md']; // the first screen already ran this
  let historyIndex = history.length;
  let draft = '';
  let pending: AbortController | null = null;
  let userAborted = false;
  let lastTabLine: string | null = null;

  const fineCursor = matchMedia('(pointer: fine)').matches;
  const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  void reduceMotion; // scrolling below is instant either way

  const promptText = () => `visitor@portfolio:${cwd}$`;

  function setCwd(next: string) {
    cwd = next;
    prompt.textContent = promptText();
    title.textContent = `visitor@portfolio: ${cwd}`;
  }

  function scrollToEnd() {
    screen.scrollTop = screen.scrollHeight;
  }

  function print(lines: TermLine[]) {
    screen.append(renderLines(lines));
    scrollToEnd();
  }

  function printText(text: string, kind?: string) {
    print([[{ t: text, k: kind }]]);
  }

  function echo(line: string) {
    const row = document.createElement('div');
    row.className = 'term-line term-echo';
    const ps = document.createElement('span');
    ps.className = 'ps';
    ps.textContent = promptText();
    row.append(ps, ` ${line}`); // appended as text: whatever was typed is never parsed as HTML
    screen.append(row);
  }

  async function fetchStats() {
    pending = new AbortController();
    userAborted = false;
    const timer = setTimeout(() => pending?.abort(), timeoutMs);
    let raw: unknown = null;
    try {
      const response = await fetch(`${apiBase}${statsPath}`, {
        headers: { Accept: 'application/json' },
        signal: pending.signal,
      });
      if (response.ok) raw = await response.json();
    } catch {
      raw = null;
    } finally {
      clearTimeout(timer);
    }
    const aborted = userAborted;
    pending = null;
    if (aborted) return printText('Cancelled.', 'dim');
    const reply = await coreCall<{ lines: TermLine[] }>('term.stats', { stats: raw, trackingSince });
    print(reply.lines);
  }

  function remember(mode: string) {
    try {
      localStorage.setItem(STORAGE_KEY, mode);
    } catch {
      /* storage blocked: ignore */
    }
  }

  async function handle(action: NonNullable<ExecResult['actions']>[number]) {
    if (action.type === 'clear') screen.replaceChildren();
    else if (action.type === 'remember' && action.mode) remember(action.mode);
    else if (action.type === 'navigate' && action.url) {
      const url = action.url;
      setTimeout(() => location.assign(url), 350);
    } else if (action.type === 'stats') await fetchStats();
  }

  async function run(line: string) {
    if (pending) {
      echo(line);
      return printText('Still waiting for the bot. Press Ctrl+C to cancel.', 'dim');
    }
    echo(line);
    const trimmed = line.trim();
    if (trimmed) {
      if (history[history.length - 1] !== trimmed) history.push(trimmed);
      historyIndex = history.length;
      draft = '';
      const result = await coreCall<ExecResult>('term.exec', {
        line,
        cwd,
        history,
        now: Math.floor(Date.now() / 1000),
      });
      print(result.lines);
      setCwd(result.cwd);
      for (const action of result.actions) await handle(action);
    }
    scrollToEnd();
  }

  async function complete() {
    const before = input.value.slice(0, input.selectionStart ?? input.value.length);
    const reply = await coreCall<{ start: number; options: string[] }>('term.complete', { line: before, cwd });
    const { start, options } = reply;
    if (!options.length) return;
    const after = input.value.slice(before.length);
    const word = before.slice(start);
    let common = options[0];
    for (const o of options) {
      while (!o.startsWith(common)) common = common.slice(0, -1);
    }
    const single = options.length === 1;
    const insert = common + (single && !common.endsWith('/') ? ' ' : '');
    if (insert.length > word.length) {
      input.value = before.slice(0, start) + insert + after;
      const caret = start + insert.length;
      input.setSelectionRange(caret, caret);
      lastTabLine = null;
      return;
    }
    // Nothing more to add: a second Tab lists the choices.
    if (lastTabLine === before && options.length > 1) {
      echo(input.value);
      printText(options.join('   '));
    }
    lastTabLine = before;
  }

  function recall(step: -1 | 1) {
    if (historyIndex === history.length) draft = input.value;
    historyIndex = Math.max(0, Math.min(history.length, historyIndex + step));
    input.value = historyIndex === history.length ? draft : history[historyIndex];
    queueMicrotask(() => input.setSelectionRange(input.value.length, input.value.length));
  }

  function clearScreen() {
    screen.replaceChildren();
  }

  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const line = input.value;
    input.value = '';
    void run(line);
  });

  input.addEventListener('keydown', (event) => {
    if (event.key === 'Tab') {
      event.preventDefault();
      void complete();
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      recall(-1);
    } else if (event.key === 'ArrowDown') {
      event.preventDefault();
      recall(1);
    } else if (event.ctrlKey && event.key.toLowerCase() === 'l') {
      event.preventDefault();
      clearScreen();
    } else if ((event.ctrlKey && event.key.toLowerCase() === 'c') || event.key === 'Escape') {
      if (pending) {
        event.preventDefault();
        userAborted = true;
        pending.abort();
      } else if (event.key !== 'Escape') {
        event.preventDefault();
        echo(`${input.value}^C`);
        input.value = '';
        scrollToEnd();
      }
    } else if (event.key !== 'Shift') {
      lastTabLine = null;
    }
  });

  // Helper buttons for soft keyboards, which have no Tab or arrow keys.
  root.querySelectorAll<HTMLButtonElement>('[data-key]').forEach((button) => {
    button.addEventListener('pointerdown', (event) => event.preventDefault()); // keep the keyboard open
    button.addEventListener('click', () => {
      const key = button.dataset.key;
      if (key === 'tab') void complete();
      else if (key === 'up') recall(-1);
      else if (key === 'down') recall(1);
      else if (key === 'clear') clearScreen();
      input.focus({ preventScroll: true });
    });
  });

  // Clickable commands (in the output and in the side panel) run as if typed.
  root.addEventListener('click', (event) => {
    const target = (event.target as Element | null)?.closest<HTMLElement>('[data-run]');
    if (!target?.dataset.run) return;
    event.preventDefault();
    void run(target.dataset.run).then(() => {
      const box = termBox.getBoundingClientRect();
      if (box.bottom > innerHeight || box.top < 0) termBox.scrollIntoView({ block: 'nearest' });
      if (fineCursor) input.focus({ preventScroll: true });
    });
  });

  screen.addEventListener('click', () => {
    // Tapping the screen focuses the prompt on desktop; on phones it would pop the keyboard open.
    if (fineCursor && !getSelection()?.toString()) input.focus({ preventScroll: true });
  });

  input.disabled = false;
  root.dataset.ready = '';
  setCwd('~');
  scrollToEnd();
  if (fineCursor) input.focus({ preventScroll: true });
}
