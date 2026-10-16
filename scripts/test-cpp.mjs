// Builds the C++ unit tests with Emscripten and runs them under Node.
import { spawnSync } from 'node:child_process';
import { mkdirSync, readdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { runEmcc } from './emcc.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outDir = join(root, 'build');
mkdirSync(outDir, { recursive: true });

const cpp = (dir) =>
  readdirSync(join(root, 'cpp', dir))
    .filter((f) => f.endsWith('.cpp'))
    .map((f) => `cpp/${dir}/${f}`);

runEmcc([
  ...cpp('src'),
  ...cpp('tests'),
  '-I', 'cpp/include', '-I', 'cpp/tests',
  '-std=c++20', '-O1', '-Wall', '-Wextra',
  '-fwasm-exceptions',
  '-sENVIRONMENT=node', '-sNODERAWFS=1',
  '-o', 'build/tests.js',
]);

const run = spawnSync(process.execPath, [join(outDir, 'tests.js')], { stdio: 'inherit' });
process.exit(run.status ?? 1);
