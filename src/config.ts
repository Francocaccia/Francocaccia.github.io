// Per-mode switches. Set `enabled: false` to hide a mode that is not finished:
// it disappears from the gateway and from the switcher menu (its page is still built).

export type ModeId = 'aviation' | 'terminal' | 'finance';

export interface ModeConfig {
  id: ModeId;
  label: string;
  path: string;
  promise: string;
  enabled: boolean;
}

export const MODES: ModeConfig[] = [
  {
    id: 'aviation',
    label: 'Aviation',
    path: '/aviation/',
    promise: 'Pilot and engineer view: route map, live-format weather, METAR and TAF decoders.',
    enabled: true,
  },
  {
    id: 'terminal',
    label: 'Terminal',
    path: '/terminal/',
    promise: 'A real shell in the browser. Type help, or click a command.',
    enabled: true,
  },
  {
    id: 'finance',
    label: 'Finance',
    path: '/finance/',
    promise: 'Investment-panel view: the trading bot, benchmarks, policy bands and a project watchlist.',
    enabled: true,
  },
];

export const STORAGE_KEY = 'fc-mode';

export const enabledModes = () => MODES.filter((m) => m.enabled);
