// The Aviation globe, browser side: drag (mouse and touch), inertia, keyboard, buttons, zoom and a live
// region. The projection, clipping and drawing are src/lib/globe.ts; the land data is fetched after the page
// has loaded. Without JavaScript the page keeps the static globe the build drew.
import { afterLoad } from '../lib/after-load';
import {
  clampZoom,
  decodeWorld,
  describeView,
  dragView,
  globeRadius,
  inertiaVelocity,
  nudgeView,
  placeAirports,
  renderGlobeInner,
  viewer,
  wrapLon,
  type Airport,
  type LonLat,
  type View,
  type World,
  type WorldFile,
} from '../lib/globe';

const root = document.querySelector<HTMLElement>('[data-globe]');
if (root) void start(root);

async function start(root: HTMLElement) {
  const stage = root.querySelector<HTMLElement>('[data-globe-stage]')!;
  const status = root.querySelector<HTMLElement>('[data-globe-status]')!;
  const svg = stage.querySelector<SVGSVGElement>('svg')!;
  const airports: Airport[] = JSON.parse(root.dataset.airports ?? '[]');
  const arcs: LonLat[][] = JSON.parse(root.dataset.arcs ?? '[]');
  const home: View = { lat: Number(root.dataset.viewLat), lon: Number(root.dataset.viewLon) };

  await afterLoad();
  let world: World;
  try {
    const response = await fetch(root.dataset.worldUrl ?? '');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    world = decodeWorld((await response.json()) as WorldFile);
  } catch {
    return; // the static globe stays, and its caption still says what it is
  }

  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  let view: View = { ...home };
  let zoom = 1;
  let width = stage.clientWidth || 420;
  let height = stage.clientHeight || width;
  let frame = 0;
  let speakTimer: number | undefined;

  // ---- drawing ----------------------------------------------------------------------------------------
  function draw() {
    frame = 0;
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    svg.innerHTML = renderGlobeInner(world, { view, zoom, width, height, airports, arcs }); // markup built here from numbers and escaped text
  }
  const schedule = () => {
    if (!frame) frame = requestAnimationFrame(draw);
  };

  // A polite summary once the globe has stopped moving, so a screen reader is not read every frame.
  function speakSoon() {
    clearTimeout(speakTimer);
    speakTimer = window.setTimeout(() => {
      const r = globeRadius(width, height, zoom);
      status.textContent = describeView(view, placeAirports(airports, viewer(view.lat, view.lon), width / 2, height / 2, r));
    }, 400);
  }

  function setView(next: View) {
    view = next;
    schedule();
    speakSoon();
  }
  function setZoom(z: number) {
    zoom = clampZoom(z);
    schedule();
    speakSoon();
  }

  const size = new ResizeObserver(() => {
    width = stage.clientWidth || width;
    height = stage.clientHeight || width;
    schedule();
  });
  size.observe(stage);

  stage.setAttribute('role', 'group');
  stage.setAttribute('aria-label', 'Route globe. Arrow keys rotate, plus and minus zoom, 0 resets.');
  stage.tabIndex = 0;
  root.dataset.ready = '';
  draw();

  // ---- drag and inertia -------------------------------------------------------------------------------
  let coasting = 0;
  const stopCoasting = () => {
    if (coasting) cancelAnimationFrame(coasting);
    coasting = 0;
  };
  let dragging = false;
  let last = { x: 0, y: 0 };
  let moves: { t: number; x: number; y: number }[] = [];

  stage.addEventListener('pointerdown', (event) => {
    if (event.pointerType === 'mouse' && event.button !== 0) return;
    stopCoasting();
    dragging = true;
    last = { x: event.clientX, y: event.clientY };
    moves = [{ t: performance.now(), x: event.clientX, y: event.clientY }];
    try {
      stage.setPointerCapture(event.pointerId); // keeps the drag going outside the globe
    } catch {
      /* a pointer that is already gone: the drag simply ends at the edge */
    }
    stage.classList.add('is-dragging');
  });
  stage.addEventListener('pointermove', (event) => {
    if (!dragging) return;
    const r = globeRadius(width, height, zoom);
    setView(dragView(view, event.clientX - last.x, event.clientY - last.y, r));
    last = { x: event.clientX, y: event.clientY };
    const now = performance.now();
    moves.push({ t: now, x: event.clientX, y: event.clientY });
    moves = moves.filter((m) => now - m.t <= 100);
  });
  const release = () => {
    if (!dragging) return;
    dragging = false;
    stage.classList.remove('is-dragging');
    if (reduced.matches || moves.length < 2) return;
    // Velocity over the last 100 ms, in degrees per millisecond, then coast with friction.
    const a = moves[0];
    const b = moves[moves.length - 1];
    const dt = b.t - a.t;
    if (dt <= 0 || performance.now() - b.t > 100) return;
    const r = globeRadius(width, height, zoom);
    const k = 180 / Math.PI / r;
    let vLon = (-(b.x - a.x) * k) / dt; // deg per ms
    let vLat = ((b.y - a.y) * k) / dt;
    let before = performance.now();
    const step = (now: number) => {
      const dtMs = Math.min(now - before, 50);
      before = now;
      // inertiaVelocity works per frame (16.7 ms); scale so the decay does not depend on the frame rate.
      vLon = inertiaVelocity(vLon * 16.7, dtMs, false) / 16.7;
      vLat = inertiaVelocity(vLat * 16.7, dtMs, false) / 16.7;
      if (vLon === 0 && vLat === 0) {
        coasting = 0;
        return;
      }
      setView({ lat: Math.max(-85, Math.min(85, view.lat + vLat * dtMs)), lon: wrapLon(view.lon + vLon * dtMs) });
      coasting = requestAnimationFrame(step);
    };
    coasting = requestAnimationFrame(step);
  };
  stage.addEventListener('pointerup', release);
  stage.addEventListener('pointercancel', release);
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) stopCoasting();
  });

  // ---- zoom: Ctrl + wheel only, so scrolling the page is never hijacked -------------------------------
  stage.addEventListener(
    'wheel',
    (event) => {
      if (!event.ctrlKey) return;
      event.preventDefault();
      setZoom(zoom * Math.exp(-event.deltaY * 0.01));
    },
    { passive: false },
  );

  // ---- keyboard and buttons ---------------------------------------------------------------------------
  let tween = 0;
  function resetView() {
    stopCoasting();
    cancelAnimationFrame(tween);
    if (reduced.matches) {
      view = { ...home };
      zoom = 1;
      schedule();
      speakSoon();
      return;
    }
    const from = { ...view, zoom };
    const t0 = performance.now();
    const run = (now: number) => {
      const t = Math.min(1, (now - t0) / 450);
      const e = 1 - (1 - t) * (1 - t) * (1 - t);
      let dLon = home.lon - from.lon;
      if (dLon > 180) dLon -= 360;
      if (dLon < -180) dLon += 360;
      view = { lat: from.lat + (home.lat - from.lat) * e, lon: wrapLon(from.lon + dLon * e) };
      zoom = from.zoom + (1 - from.zoom) * e;
      schedule();
      if (t < 1) tween = requestAnimationFrame(run);
      else speakSoon();
    };
    tween = requestAnimationFrame(run);
  }

  function act(name: string) {
    stopCoasting();
    switch (name) {
      case 'left': return setView(nudgeView(view, -10, 0));
      case 'right': return setView(nudgeView(view, 10, 0));
      case 'up': return setView(nudgeView(view, 0, 10));
      case 'down': return setView(nudgeView(view, 0, -10));
      case 'in': return setZoom(zoom * 1.4);
      case 'out': return setZoom(zoom / 1.4);
      case 'reset': return resetView();
    }
  }

  stage.addEventListener('keydown', (event) => {
    if (event.ctrlKey || event.altKey || event.metaKey) return;
    const keys: Record<string, string> = {
      ArrowLeft: 'left',
      ArrowRight: 'right',
      ArrowUp: 'up',
      ArrowDown: 'down',
      '+': 'in',
      '=': 'in',
      '-': 'out',
      _: 'out',
      '0': 'reset',
      Home: 'reset',
    };
    const name = keys[event.key];
    if (!name) return; // everything else, Tab included, is left alone
    event.preventDefault();
    act(name);
  });
  root.querySelectorAll<HTMLButtonElement>('[data-globe-act]').forEach((button) => {
    button.addEventListener('click', () => act(button.dataset.globeAct ?? ''));
  });
}
