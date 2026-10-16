// Compiles the C++ core to WebAssembly: src/generated/core.mjs + core.wasm.
// Pass --if-stale to skip the build when the output is newer than every source.
import { existsSync, mkdirSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { runEmcc } from './emcc.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outDir = join(root, 'src', 'generated');
const outFile = join(outDir, 'core.mjs');

function walk(dir) {
  return readdirSync(dir, { withFileTypes: true }).flatMap((entry) => {
    const path = join(dir, entry.name);
    return entry.isDirectory() ? walk(path) : [path];
  });
}

const sources = walk(join(root, 'cpp', 'src')).filter((f) => f.endsWith('.cpp'));
sources.push(join(root, 'cpp', 'wasm', 'exports.cpp'));
const everything = [...walk(join(root, 'cpp')), join(root, 'scripts', 'build-wasm.mjs')];

if (process.argv.includes('--if-stale') && existsSync(outFile)) {
  const built = statSync(outFile).mtimeMs;
  if (everything.every((f) => statSync(f).mtimeMs <= built)) {
    console.log('C++ core is up to date.');
    process.exit(0);
  }
}

mkdirSync(outDir, { recursive: true });
console.log(`Compiling ${sources.length} C++ files to WebAssembly...`);

runEmcc([
  ...sources.map((f) => relative(root, f)),
  '-I', 'cpp/include',
  '-std=c++20', '-O2', '-Wall', '-Wextra',
  '-fwasm-exceptions',
  '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createCore',
  '-sENVIRONMENT=web,node',
  '-sEXPORTED_FUNCTIONS=_fc_call,_fc_free',
  '-sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString',
  '-sALLOW_MEMORY_GROWTH=1', '-sFILESYSTEM=0',
  '-o', relative(root, outFile),
].map(String));

console.log('Built src/generated/core.mjs');
