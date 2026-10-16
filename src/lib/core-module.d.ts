// Types for the Emscripten-generated module (it is not checked in).
declare module '*/generated/core.mjs' {
  interface CoreOptions {
    instantiateWasm?: (imports: WebAssembly.Imports, receive: (instance: WebAssembly.Instance) => void) => unknown;
  }
  const createCore: (options?: CoreOptions) => Promise<{
    ccall(name: string, returnType: 'number', argTypes: string[], args: unknown[]): number;
    UTF8ToString(ptr: number): string;
    _fc_free(ptr: number): void;
  }>;
  export default createCore;
}
