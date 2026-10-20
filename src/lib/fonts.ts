// Self-hosted fonts, one set per view. Each set is a few woff2 files (the fontsource packages ship woff as well, and
// their CSS would pull both into the build) plus, for every family, a metric-matched fallback face: until the web
// font arrives the page is set in Arial, Georgia or Courier New scaled and shifted to the web font's size, so the
// swap does not move the text. The numbers come from @capsizecss/metrics (a build-time dev dependency).
import arial from '@capsizecss/metrics/arial';
import courierNew from '@capsizecss/metrics/courierNew';
import georgia from '@capsizecss/metrics/georgia';
import b612 from '@capsizecss/metrics/b612';
import b612Mono from '@capsizecss/metrics/b612Mono';
import geistMono from '@capsizecss/metrics/geistMono';
import ibmPlexMono from '@capsizecss/metrics/iBMPlexMono';
import ibmPlexSans from '@capsizecss/metrics/iBMPlexSans';
import instrumentSans from '@capsizecss/metrics/instrumentSans';
import instrumentSerif from '@capsizecss/metrics/instrumentSerif';
import jetBrainsMono from '@capsizecss/metrics/jetBrainsMono';
import vt323 from '@capsizecss/metrics/vT323';

import b612Bold from '@fontsource/b612/files/b612-latin-700-normal.woff2?url';
import b612Regular from '@fontsource/b612/files/b612-latin-400-normal.woff2?url';
import b612MonoBold from '@fontsource/b612-mono/files/b612-mono-latin-700-normal.woff2?url';
import b612MonoRegular from '@fontsource/b612-mono/files/b612-mono-latin-400-normal.woff2?url';
import geistMono400 from '@fontsource/geist-mono/files/geist-mono-latin-400-normal.woff2?url';
import geistMono500 from '@fontsource/geist-mono/files/geist-mono-latin-500-normal.woff2?url';
import plexMono400 from '@fontsource/ibm-plex-mono/files/ibm-plex-mono-latin-400-normal.woff2?url';
import plexMono600 from '@fontsource/ibm-plex-mono/files/ibm-plex-mono-latin-600-normal.woff2?url';
import plexSans400 from '@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-400-normal.woff2?url';
import plexSans600 from '@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-600-normal.woff2?url';
import instrumentSans400 from '@fontsource/instrument-sans/files/instrument-sans-latin-400-normal.woff2?url';
import instrumentSans500 from '@fontsource/instrument-sans/files/instrument-sans-latin-500-normal.woff2?url';
import instrumentSans600 from '@fontsource/instrument-sans/files/instrument-sans-latin-600-normal.woff2?url';
import instrumentSerif400 from '@fontsource/instrument-serif/files/instrument-serif-latin-400-normal.woff2?url';
import instrumentSerifItalic from '@fontsource/instrument-serif/files/instrument-serif-latin-400-italic.woff2?url';
import jetBrains400 from '@fontsource/jetbrains-mono/files/jetbrains-mono-latin-400-normal.woff2?url';
import jetBrains700 from '@fontsource/jetbrains-mono/files/jetbrains-mono-latin-700-normal.woff2?url';
import vt32300 from '@fontsource/vt323/files/vt323-latin-400-normal.woff2?url';

interface Metrics {
  ascent: number;
  descent: number;
  lineGap: number;
  unitsPerEm: number;
  xWidthAvg: number;
}

interface Family {
  name: string;
  metrics: Metrics;
  fallback: { local: string[]; metrics: Metrics };
}

interface Face {
  family: Family;
  weight: number;
  style?: 'italic';
  url: string;
  /** Used above the fold: preloaded, so the text does not move when it arrives. */
  preload?: boolean;
}

const sans = { local: ['Arial', 'Helvetica', 'Liberation Sans'], metrics: arial };
const serif = { local: ['Georgia', 'Times New Roman'], metrics: georgia };
const mono = { local: ['Courier New', 'Liberation Mono'], metrics: courierNew };

const PlexSans: Family = { name: 'IBM Plex Sans', metrics: ibmPlexSans, fallback: sans };
const PlexMono: Family = { name: 'IBM Plex Mono', metrics: ibmPlexMono, fallback: mono };
const InstrumentSans: Family = { name: 'Instrument Sans', metrics: instrumentSans, fallback: sans };
const InstrumentSerif: Family = { name: 'Instrument Serif', metrics: instrumentSerif, fallback: serif };
const GeistMono: Family = { name: 'Geist Mono', metrics: geistMono, fallback: mono };
const JetBrainsMono: Family = { name: 'JetBrains Mono', metrics: jetBrainsMono, fallback: mono };
const VT323: Family = { name: 'VT323', metrics: vt323, fallback: mono };
const B612: Family = { name: 'B612', metrics: b612, fallback: sans };
const B612Mono: Family = { name: 'B612 Mono', metrics: b612Mono, fallback: mono };

/** The CSS that makes `family` fall back to a resized system font until the web font has loaded. */
function fallbackFace(f: Family): string {
  const px = f.metrics;
  const fb = f.fallback.metrics;
  const sizeAdjust = px.xWidthAvg / px.unitsPerEm / (fb.xWidthAvg / fb.unitsPerEm);
  const pct = (v: number) => `${(Math.round(v * 1000) / 10).toString()}%`;
  const over = (v: number) => pct(Math.abs(v) / (px.unitsPerEm * sizeAdjust));
  return (
    `@font-face{font-family:'${f.name} Fallback';src:${f.fallback.local.map((l) => `local('${l}')`).join(',')};` +
    `size-adjust:${pct(sizeAdjust)};ascent-override:${over(px.ascent)};descent-override:${over(px.descent)};line-gap-override:${over(px.lineGap)}}`
  );
}

function build(faces: Face[]) {
  const families = [...new Set(faces.map((f) => f.family))];
  const css =
    faces
      .map(
        (f) =>
          `@font-face{font-family:'${f.family.name}';font-style:${f.style ?? 'normal'};font-weight:${f.weight};font-display:swap;src:url(${f.url}) format('woff2')}`,
      )
      .join('') + families.map(fallbackFace).join('');
  return { css, preload: faces.filter((f) => f.preload).map((f) => f.url) };
}

export const fontSets = {
  gateway: build([
    { family: PlexSans, weight: 400, url: plexSans400, preload: true },
    { family: PlexSans, weight: 600, url: plexSans600, preload: true },
    { family: PlexMono, weight: 400, url: plexMono400, preload: true },
    { family: PlexMono, weight: 600, url: plexMono600, preload: true },
  ]),
  finance: build([
    { family: InstrumentSerif, weight: 400, url: instrumentSerif400, preload: true },
    { family: InstrumentSerif, weight: 400, style: 'italic', url: instrumentSerifItalic },
    { family: InstrumentSans, weight: 400, url: instrumentSans400, preload: true },
    { family: InstrumentSans, weight: 500, url: instrumentSans500 },
    { family: InstrumentSans, weight: 600, url: instrumentSans600 },
    { family: GeistMono, weight: 400, url: geistMono400, preload: true },
    { family: GeistMono, weight: 500, url: geistMono500 },
  ]),
  terminal: build([
    { family: VT323, weight: 400, url: vt32300, preload: true },
    { family: JetBrainsMono, weight: 400, url: jetBrains400, preload: true },
    { family: JetBrainsMono, weight: 700, url: jetBrains700, preload: true },
  ]),
  aviation: build([
    { family: B612, weight: 400, url: b612Regular, preload: true },
    { family: B612, weight: 700, url: b612Bold, preload: true },
    { family: B612Mono, weight: 400, url: b612MonoRegular, preload: true },
    { family: B612Mono, weight: 700, url: b612MonoBold, preload: true },
  ]),
};
