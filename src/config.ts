// Per-mode switches. Set `enabled: false` to hide a mode that is not finished:
// it disappears from the gateway and from the switcher menu (its page is still built).

export type ModeId = 'aviation' | 'terminal' | 'finance';

export interface ModeConfig {
  id: ModeId;
  label: string;
  path: string;
  /** One line for the gateway door. */
  promise: string;
  /** What is inside, shown under the promise. */
  inside: string[];
  enabled: boolean;
}

export const MODES: ModeConfig[] = [
  {
    id: 'aviation',
    label: 'Aviation',
    path: '/aviation/',
    promise: 'The pilot and engineer read: route, weather and the math behind a flight.',
    inside: ['Route map, SABE to KLYH to KABE', 'METAR and TAF decoders', 'Crosswind calculator', 'Pilot card'],
    enabled: true,
  },
  {
    id: 'terminal',
    label: 'Terminal',
    path: '/terminal/',
    promise: 'A real shell in the browser. Type help, or click a command.',
    inside: ['Projects, experience and contact as files', 'ls, cat, grep, tree and more', 'Click-to-run command list'],
    enabled: true,
  },
  {
    id: 'finance',
    label: 'Finance',
    path: '/finance/',
    promise: 'The investment-panel read: a paper-trading bot against its benchmarks.',
    inside: ['AutoTradingBot candles vs SPY and 60/40', 'Project watchlist and skill heatmap', 'Policy bands and risk controls'],
    enabled: true,
  },
];

export const STORAGE_KEY = 'fc-mode';

export const enabledModes = () => MODES.filter((m) => m.enabled);
