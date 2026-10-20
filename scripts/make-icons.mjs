// Dev-time script: draws the favicon's "F" as PNG files, without any image library, so browsers that
// do not use the SVG icon (older ones, and iOS home screens) get one too.
//   public/apple-touch-icon.png  180x180, square (iOS rounds the corners itself)
//   public/favicon.ico           32x32 PNG inside an ICO container
// The shapes are the ones in public/favicon.svg (a 32x32 viewBox): a dark square and an amber "F" made of
// three bars. Run with `npm run build:icons`.
import { writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateSync } from 'node:zlib';

const root = join(dirname(fileURLToPath(import.meta.url)), '..', 'public');
const BG = [0x0e, 0x10, 0x13];
const FG = [0xff, 0xb0, 0x00];
// Bars of the glyph in viewBox units (stroke width 3, square caps): vertical, top and middle.
const BARS = [
  [7.5, 7.5, 10.5, 24.5],
  [7.5, 7.5, 23.5, 10.5],
  [7.5, 14.5, 20.5, 17.5],
];

function render(size, rounded) {
  const SS = 4; // supersampling, for smooth edges
  const px = Buffer.alloc(size * size * 4);
  const r = 6; // corner radius in viewBox units
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      let cover = 0;
      let glyph = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const u = ((x + (sx + 0.5) / SS) / size) * 32;
          const v = ((y + (sy + 0.5) / SS) / size) * 32;
          let inside = true;
          if (rounded) {
            const cx = Math.min(Math.max(u, r), 32 - r);
            const cy = Math.min(Math.max(v, r), 32 - r);
            inside = Math.hypot(u - cx, v - cy) <= r;
          }
          if (!inside) continue;
          cover++;
          if (BARS.some(([x0, y0, x1, y1]) => u >= x0 && u <= x1 && v >= y0 && v <= y1)) glyph++;
        }
      }
      const total = SS * SS;
      const o = (y * size + x) * 4;
      const g = cover ? glyph / cover : 0;
      for (let c = 0; c < 3; c++) px[o + c] = Math.round(BG[c] * (1 - g) + FG[c] * g);
      px[o + 3] = Math.round((cover / total) * 255);
    }
  }
  return px;
}

const crcTable = new Uint32Array(256).map((_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
const crc32 = (buf) => {
  let c = 0xffffffff;
  for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
};

function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'ascii');
  data.copy(out, 8);
  out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
  return out;
}

function png(size, rounded) {
  const pixels = render(size, rounded);
  const raw = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) {
    raw[y * (size * 4 + 1)] = 0; // filter: none
    pixels.copy(raw, y * (size * 4 + 1) + 1, y * size * 4, (y + 1) * size * 4);
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(size, 0);
  header.writeUInt32BE(size, 4);
  header[8] = 8; // bit depth
  header[9] = 6; // RGBA
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

writeFileSync(join(root, 'apple-touch-icon.png'), png(180, false));

const small = png(32, true);
const ico = Buffer.alloc(22 + small.length);
ico.writeUInt16LE(0, 0); // reserved
ico.writeUInt16LE(1, 2); // type: icon
ico.writeUInt16LE(1, 4); // one image
ico[6] = 32; // width
ico[7] = 32; // height
ico.writeUInt16LE(1, 10); // planes
ico.writeUInt16LE(32, 12); // bits per pixel
ico.writeUInt32LE(small.length, 14);
ico.writeUInt32LE(22, 18); // offset of the image
small.copy(ico, 22);
writeFileSync(join(root, 'favicon.ico'), ico);
console.log('wrote public/apple-touch-icon.png and public/favicon.ico');
