/** Resolves once the page and its fonts and styles have finished loading. Heavy, non-critical work waits for it. */
export const afterLoad = (): Promise<void> =>
  document.readyState === 'complete' ? Promise.resolve() : new Promise((resolve) => addEventListener('load', () => resolve(), { once: true }));
