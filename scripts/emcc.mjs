// Locates the Emscripten C++ compiler (em++) and runs it. Shared by the build and test scripts.
//
// Lookup order: $EMSDK, then ~/emsdk (a local install), then `em++` on PATH
// (what CI provides). On Windows the SDK's own Python runs em++.py directly,
// which avoids .bat quoting problems.
import { spawnSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { homedir } from 'node:os';
import { join } from 'node:path';

function findSdk() {
  for (const dir of [process.env.EMSDK, join(homedir(), 'emsdk')]) {
    if (dir && existsSync(join(dir, 'upstream', 'emscripten', 'em++.py'))) return dir;
  }
  return null;
}

function sdkPython(sdk) {
  const root = join(sdk, 'python');
  if (existsSync(root)) {
    for (const entry of readdirSync(root)) {
      for (const exe of ['python.exe', join('bin', 'python3')]) {
        const candidate = join(root, entry, exe);
        if (existsSync(candidate)) return candidate;
      }
    }
  }
  return process.platform === 'win32' ? 'python' : 'python3';
}

export function emccCommand() {
  const sdk = findSdk();
  if (sdk) {
    return {
      command: sdkPython(sdk),
      prefix: [join(sdk, 'upstream', 'emscripten', 'em++.py')],
      env: { ...process.env, EM_CONFIG: join(sdk, '.emscripten'), EMSDK: sdk },
      label: `emsdk at ${sdk}`,
    };
  }
  return { command: 'em++', prefix: [], env: process.env, label: 'em++ on PATH' };
}

export function runEmcc(args) {
  const { command, prefix, env, label } = emccCommand();
  const result = spawnSync(command, [...prefix, ...args], { stdio: 'inherit', env });
  if (result.error) {
    throw new Error(
      `Could not run the Emscripten compiler (${label}): ${result.error.message}\n` +
        'Install the Emscripten SDK (https://emscripten.org/docs/getting_started/downloads.html) ' +
        'into ~/emsdk, or set $EMSDK.',
    );
  }
  if (result.status !== 0) process.exit(result.status ?? 1);
}
