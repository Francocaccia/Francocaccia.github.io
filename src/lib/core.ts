// Loader for the C++ core (src/generated/core.mjs + core.wasm, built by scripts/build-wasm.mjs).
// Works at build time under Node (pages pre-render from it) and in the browser.
import createCore from '../generated/core.mjs';

interface CoreModule {
  ccall(name: string, returnType: 'number', argTypes: string[], args: unknown[]): number;
  UTF8ToString(ptr: number): string;
  _fc_free(ptr: number): void;
}

type Reply<T> = { ok: true; result: T } | { ok: false; error: string };

const MAX_INPUT_BYTES = 48 * 1024;

let loading: Promise<CoreModule> | undefined;

export function loadCore(): Promise<CoreModule> {
  loading ??= (async () => {
    if (import.meta.env.SSR) {
      // Bundled server chunks cannot locate the .wasm next to themselves, so hand over the bytes.
      const { readFile } = await import('node:fs/promises');
      const { join } = await import('node:path');
      const bytes = await readFile(join(process.cwd(), 'src', 'generated', 'core.wasm'));
      return createCore({
        instantiateWasm(imports, receive) {
          WebAssembly.instantiate(bytes, imports).then(({ instance }) => receive(instance));
          return {};
        },
      });
    }
    return createCore();
  })();
  return loading;
}

/** Calls one C++ operation. Throws if the core reports an error. */
export async function coreCall<T = unknown>(op: string, input?: unknown): Promise<T> {
  const core = await loadCore();
  const json = input === undefined ? '' : JSON.stringify(input);
  // ccall copies string arguments onto the 64 KB wasm stack; past that the module traps and stays broken.
  if (new TextEncoder().encode(json).length > MAX_INPUT_BYTES) throw new Error(`core.${op}: input is too long`);
  const ptr = core.ccall('fc_call', 'number', ['string', 'string'], [op, json]);
  const reply = JSON.parse(core.UTF8ToString(ptr)) as Reply<T>;
  core._fc_free(ptr);
  if (!reply.ok) throw new Error(`core.${op}: ${reply.error}`);
  return reply.result;
}

export const coreVersion = async (): Promise<string> => (await coreCall<{ core: string }>('version')).core;
