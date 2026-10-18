// Turns the shell engine's structured output into DOM. Text always goes in with
// textContent, so nothing a visitor types (or the engine echoes) can become markup.

export interface TermSpan {
  t: string;
  /** dim, accent, error, match, pos, neg, link, cmd */
  k?: string;
  h?: string;
  c?: string;
}
export type TermLine = TermSpan[];

/** Links may only go to https, mailto, or a path on this site. */
export const SAFE_HREF = /^(https:\/\/|mailto:|\/(?!\/))/;

export function renderSpan(s: TermSpan): Node {
  if (s.k === 'link' && s.h && SAFE_HREF.test(s.h)) {
    const a = document.createElement('a');
    a.href = s.h;
    a.textContent = s.t;
    if (!s.h.startsWith('/')) {
      a.target = '_blank';
      a.rel = 'noopener noreferrer';
    }
    return a;
  }
  if (s.k === 'cmd' && s.c) {
    // Clickable command: the button holds the visible name, trailing padding stays outside it.
    const name = s.t.trimEnd();
    const frag = document.createDocumentFragment();
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'cmd';
    button.dataset.run = s.c;
    button.textContent = name;
    frag.append(button, s.t.slice(name.length));
    return frag;
  }
  const span = document.createElement('span');
  if (s.k) span.className = `k-${s.k}`;
  span.textContent = s.t;
  return span;
}

export function renderLines(lines: TermLine[]): DocumentFragment {
  const frag = document.createDocumentFragment();
  for (const line of lines) {
    const row = document.createElement('div');
    row.className = 'term-line';
    for (const span of line) row.append(renderSpan(span));
    frag.append(row);
  }
  return frag;
}
