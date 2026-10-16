// Build-time helpers that ask the C++ core for chart markup.
import { coreCall } from './core';

export interface ChartResult {
  svg: string;
  table: string;
  points: { x: number; label: string; o: number; h: number; l: number; c: number; spy: number; mix: number }[];
  plot: { x0: number; x1: number; y0: number; y1: number; w: number; h: number };
  summary: string;
  n: number;
}

/** Fixed seed: the illustrative series is identical on every build. */
export const ILLUSTRATIVE_SEED = 7;

/** Wide and narrow variants; CSS shows the one that fits the container. */
export const SIZES = {
  wide: { width: 720, height: 360, idPrefix: 'fcz-w' },
  narrow: { width: 360, height: 300, idPrefix: 'fcz-n' },
} as const;

export const illustrativeChart = (variant: keyof typeof SIZES, days = 30): Promise<ChartResult> =>
  coreCall<ChartResult>('chart.illustrative', { seed: ILLUSTRATIVE_SEED, days, ...SIZES[variant] });

export const previewCandles = (width: number, height: number, seed: number, days: number): Promise<ChartResult> =>
  coreCall<ChartResult>('chart.illustrative', { seed, days, width, height, compact: true, idPrefix: `prev-${seed}` });
