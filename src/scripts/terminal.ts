// Terminal view, browser side. The shell itself (filesystem, commands, completion) is the
// C++ core running as WebAssembly; this file only handles input, history and the DOM.
import { STORAGE_KEY } from '../config';
import { afterLoad } from '../lib/after-load';
import { renderLines, type TermLine } from '../lib/term-render';

interface ExecResult {
  lines: TermLine[];
  actions: { type: string; url?: string; mode?: string }[];
  cwd: string;
}

const MAX_LINE = 1000; // matches the shell's own limit
const MAX_HISTORY = 100; // the history is sent with every command

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
    historyPath = '',
    historyLive = 'false',
    trackingSince = '',
    modes = '',
  } = root.dataset;
  const timeoutMs = Number(root.dataset.timeout) || 75000;

  // The cheat sheet is closed in the HTML so nothing moves on phones; on wide screens there is room for it.
  if (cheat && matchMedia('(min-width: 900px)').matches) cheat.open = true;

  const site = JSON.parse(document.getElementById('term-site')!.textContent!);
  // The first screen is already on the page. The ~125 KB (gzipped) core loads after the page does, and the
  // command buttons are wired up straight away: a click before the shell is ready waits for it, and says so
  // if it never comes.
  let coreCall!: typeof import('../lib/core').coreCall;
  let isReady = false;
  let failed = false;
  const ready = (async () => {
    await afterLoad();
    ({ coreCall } = await import('../lib/core'));
    await coreCall('term.init', {
      site,
      modes: modes.split(',').filter(Boolean),
      metars: JSON.parse(root.dataset.metars ?? '{}'),
    });
    isReady = true;
  })();

  let cwd = '~';
  const history: string[] = ['projects', 'stats', 'experience']; // the first screen already ran these
  let historyIndex = history.length;
  let draft = '';
  let pending: AbortController | null = null;
  let userAborted = false;
  let lastTabLine: string | null = null;

  const fineCursor = matchMedia('(pointer: fine)').matches;

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

  ready.catch(() => {
    failed = true;
    root.dataset.failed = '';
    printText('The shell failed to load. Reload the page to try again.', 'error');
  });

  function echo(line: string) {
    const row = document.createElement('div');
    row.className = 'term-line term-echo';
    const ps = document.createElement('span');
    ps.className = 'ps';
    ps.textContent = promptText();
    row.append(ps, ` ${line}`); // appended as text: whatever was typed is never parsed as HTML
    screen.append(row);
  }

  // The bot's public feeds. A failed or empty request gives null, never an exception.
  async function fetchBot(signal: AbortSignal): Promise<{ raw: unknown; history: unknown }> {
    const get = async (path: string): Promise<unknown> => {
      try {
        const response = await fetch(`${apiBase}${path}`, { headers: { Accept: 'application/json' }, signal });
        return response.ok ? await response.json() : null;
      } catch {
        return null;
      }
    };
    // The history feed is only requested once the bot publishes it (bot.historyLive in the data file).
    const [raw, history] = await Promise.all([get(statsPath), historyLive === 'true' ? get(historyPath) : Promise.resolve(null)]);
    return { raw, history };
  }

  const cancelKey = root.querySelector<HTMLButtonElement>('[data-key="cancel"]');
  const showCancel = (on: boolean) => {
    if (cancelKey) cancelKey.hidden = !on;
  };

  async function fetchStats() {
    pending = new AbortController();
    showCancel(true);
    userAborted = false;
    const timer = setTimeout(() => pending?.abort(), timeoutMs);
    const { raw, history } = await fetchBot(pending.signal);
    clearTimeout(timer);
    const aborted = userAborted;
    pending = null;
    showCancel(false);
    if (aborted) return printText('Cancelled.', 'dim');
    const reply = await coreCall<{ lines: TermLine[] }>('term.stats', { stats: raw, trackingSince, history });
    print(reply.lines);
  }

  // The first screen shows the stats block with placeholders. Swap in the real numbers without
  // changing its size, so nothing on the page moves.
  async function refreshStatsBlock() {
    const block = root.querySelector<HTMLElement>('[data-stats-block]');
    if (!block) return;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    const { raw, history } = await fetchBot(controller.signal);
    clearTimeout(timer);
    const reply = await coreCall<{ lines: TermLine[] }>('term.stats', { stats: raw, trackingSince, history, pad: true });
    block.replaceChildren(renderLines(reply.lines));
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
      return printText('Still waiting for the bot. Press Ctrl+C, or the Cancel key, to cancel.', 'dim');
    }
    echo(line.length > MAX_LINE ? `${line.slice(0, MAX_LINE)}...` : line);
    const trimmed = line.trim();
    if (trimmed) {
      if (history[history.length - 1] !== trimmed) history.push(trimmed.slice(0, MAX_LINE));
      if (history.length > MAX_HISTORY) history.splice(0, history.length - MAX_HISTORY);
      historyIndex = history.length;
      draft = '';
      if (!isReady && !failed) printText('Loading the shell...', 'dim');
      try {
        await ready;
        const result = await coreCall<ExecResult>('term.exec', {
          line: line.slice(0, MAX_LINE + 1),
          cwd,
          history,
          now: Math.floor(Date.now() / 1000),
        });
        print(result.lines);
        setCwd(result.cwd);
        for (const action of result.actions) await handle(action);
      } catch {
        // A failed load has already printed its own message.
        if (!failed) printText('That command could not run.', 'error');
      }
    }
    scrollToEnd();
  }

  async function complete() {
    if (!isReady) return;
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

  // Tab completes names, which would trap keyboard users. It only completes when there is text to
  // complete, never with Shift, and Escape followed by Tab always leaves the prompt.
  let leaveNext = false;
  input.addEventListener('keydown', (event) => {
    if (event.key === 'Tab') {
      if (event.shiftKey || leaveNext || input.value.trim() === '') {
        leaveNext = false;
        return;
      }
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
      const copying = event.key !== 'Escape' && input.selectionStart !== input.selectionEnd;
      if (copying && !pending) return; // Ctrl+C with selected text is a copy, not an interrupt
      if (pending) {
        event.preventDefault();
        userAborted = true;
        pending.abort();
      } else if (event.key === 'Escape') {
        leaveNext = true;
        return;
      } else {
        event.preventDefault();
        echo(`${input.value}^C`);
        input.value = '';
        scrollToEnd();
      }
    } else if (event.key !== 'Shift') {
      lastTabLine = null;
      leaveNext = false;
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
      else if (key === 'cancel' && pending) {
        userAborted = true;
        pending.abort();
      }
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

  // The prompt is not focused automatically: that would skip the skip link and drop a screen-reader user
  // into the middle of the page. Clicking a command, or the screen, focuses it on desktop.
  void ready.then(() => {
    input.disabled = false;
    root.dataset.ready = '';
    setCwd('~');
    void refreshStatsBlock().catch(() => {
      /* the first-screen placeholders stay */
    });
  }).catch(() => {
    /* reported above */
  });
}
