// Build-time helpers that ask the C++ core for chart markup.
import { coreCall } from './core';

export interface ChartPoint {
  x: number;
  label: string;
  o: number;
  h: number;
  l: number;
  c: number;
  spy: number;
  mix: number;
  /** Bot close minus SPY close, in index points. */
  ex: number;
  /** Open to close, percent. */
  chg: number;
  /** One sentence about this day. Starts with "Illustrative." while the data is illustrative. */
  readout: string;
}

export interface ChartResult {
  svg: string;
  /** The strip under the chart (bot close minus SPY close), when excessHeight was asked for. */
  excess?: string;
  table: string;
  points: ChartPoint[];
  plot: { x0: number; x1: number; y0: number; y1: number; w: number; h: number };
  exPlot?: { y0: number; y1: number; h: number };
  summary: string;
  n: number;
}

/** Fixed seed: the illustrative series is identical on every build. */
export const ILLUSTRATIVE_SEED = 7;

/** Three sizes; CSS shows the one that fits the container (phone, tablet, desktop). */
export const SIZES = {
  narrow: { width: 360, height: 300, excessHeight: 64, idPrefix: 'fcz-n' },
  mid: { width: 720, height: 340, excessHeight: 72, idPrefix: 'fcz-m' },
  wide: { width: 1100, height: 360, excessHeight: 76, idPrefix: 'fcz-w' },
} as const;

export type Variant = keyof typeof SIZES;

export const illustrativeChart = (variant: Variant, days = 30): Promise<ChartResult> =>
  coreCall<ChartResult>('chart.illustrative', { seed: ILLUSTRATIVE_SEED, days, ...SIZES[variant] });

export const previewCandles = (width: number, height: number, seed: number, days: number): Promise<ChartResult> =>
  coreCall<ChartResult>('chart.illustrative', { seed, days, width, height, compact: true, idPrefix: `prev-${seed}` });
