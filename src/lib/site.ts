import data from '../data/site.json';

export const site = data;
export type Site = typeof data;
export type Project = Site['projects'][number];

/** Placeholders in the data file start with "TODO". They render as absent, never as real values. */
export const isTodo = (value: unknown): boolean =>
  value == null || (typeof value === 'string' && value.startsWith('TODO'));

/** Returns the value, or null when it is still a placeholder. */
export const real = (value: string | null | undefined): string | null => (isTodo(value) ? null : (value as string));

export const withBase = (path: string): string => {
  const base = import.meta.env.BASE_URL.replace(/\/$/, '');
  return `${base}${path}`;
};
