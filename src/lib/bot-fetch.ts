// Fetches one of the bot's public JSON feeds. The free host sleeps when idle, so the first request can
// get a quick 502 or 503 (or no answer at all) while it wakes. Those are retried, quietly, with a growing
// pause; the whole thing still ends inside `totalMs`. A 4xx answer (the feed is not there) is final.

const sleep = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

function within(ms: number, signal?: AbortSignal): AbortSignal {
  const timeout = AbortSignal.timeout(ms);
  return signal && 'any' in AbortSignal ? AbortSignal.any([timeout, signal]) : timeout;
}

export async function fetchBotJson(url: string, totalMs: number, signal?: AbortSignal): Promise<unknown> {
  const deadline = Date.now() + totalMs;
  let pause = 5000;
  let last: unknown = new Error('no answer');
  for (let attempt = 0; attempt < 3; attempt++) {
    const remaining = deadline - Date.now();
    if (remaining <= 0) break;
    try {
      const response = await fetch(url, { headers: { Accept: 'application/json' }, signal: within(remaining, signal) });
      if (response.ok) return await response.json();
      last = new Error(`HTTP ${response.status}`);
      if (response.status < 500 && response.status !== 429) throw last; // not a wake-up problem: do not retry
    } catch (error) {
      if (signal?.aborted || last === error) throw error; // cancelled by the user, or a final 4xx
      last = error;
    }
    if (Date.now() + pause >= deadline) break;
    await sleep(pause);
    pause = Math.min(pause * 2, 10000);
  }
  throw last;
}
