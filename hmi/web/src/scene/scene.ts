/**
 * Escena del robot: vista lateral 2D y vista 3D, portadas del Simulador_Balancin
 * (canvas escrito a mano, sin librerías).
 *
 * - 2D: la vista que mide. θ se lee contra la vertical punteada; la línea ámbar es el
 *   ángulo deseado (θ ref); el arco azul sobre la rueda es el PWM aplicado; la regla del
 *   piso se arrastra con la posición del eje.
 * - 3D: para presentar el robot. Arrastrar gira la cámara, la rueda del ratón acerca,
 *   "Reencuadrar" la devuelve al inicio. Cada rueda gira con su propia RPM.
 *
 * Se redibuja sólo cuando cambia la pose, la cámara o el tamaño.
 */
import { BLOCKS, COM_HEIGHT, TOP_HEIGHT, TRACK, WHEEL_RADIUS, type Block } from './geometry';
import type { Pose } from './pose';
import { cssVar } from '../ui/theme';

type V3 = [number, number, number];
interface Face {
  p: V3[];
  n: V3;
  col: string;
  edge?: boolean;
}

const R = WHEEL_RADIUS;
const CAM_START = { az: -0.62, el: 0.26, zoom: 1 };

const n3 = (v: V3): V3 => {
  const k = Math.hypot(v[0], v[1], v[2]) || 1;
  return [v[0] / k, v[1] / k, v[2] / k];
};
const sub3 = (a: V3, b: V3): V3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross3 = (a: V3, b: V3): V3 => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot3 = (a: V3, b: V3) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const neg3 = (v: V3): V3 => [-v[0], -v[1], -v[2]];
const LIGHT = n3([-0.34, 0.88, 0.44]);

function shade(hex: string, n: V3): string {
  // ambiente alto: el color identifica el bloque y no debe perderse en la sombra
  const k = 0.62 + 0.38 * Math.max(0, dot3(n, LIGHT));
  const c = (i: number) => Math.round(Math.min(255, parseInt(hex.slice(i, i + 2), 16) * k));
  return `rgb(${c(1)},${c(3)},${c(5)})`;
}

function store(key: string, value?: string): string | null {
  try {
    if (value !== undefined) localStorage.setItem(key, value);
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export class RobotScene {
  private canvas: HTMLCanvasElement;
  private view: '2d' | '3d' = store('escena.vista') === '3d' ? '3d' : '2d';
  private cam = { ...CAM_START };
  private pose: Pose | null = null;
  private req = 0;
  private W = 300;
  private H = 300;
  private seg2d: HTMLButtonElement;
  private seg3d: HTMLButtonElement;
  private btnReframe: HTMLButtonElement;

  constructor(host: HTMLElement, opts: { onRecenter?: () => void; caption?: string } = {}) {
    host.classList.add('scene');
    host.innerHTML = `
      <div class="scene-bar">
        <div class="seg" role="group" aria-label="Vista">
          <button type="button" data-v="2d">2D</button><button type="button" data-v="3d">3D</button>
        </div>
        <button class="btn small" type="button" data-a="reencuadrar" title="Cámara a la posición inicial">Reencuadrar</button>
        ${opts.onRecenter ? '<button class="btn small" type="button" data-a="centrar" title="Poner el eje en x = 0">Centrar</button>' : ''}
      </div>
      <div class="scene-canvas"><canvas></canvas></div>
      ${opts.caption ? `<div class="scene-cap">${opts.caption}</div>` : ''}`;
    this.canvas = host.querySelector('canvas')!;
    this.seg2d = host.querySelector('[data-v="2d"]')!;
    this.seg3d = host.querySelector('[data-v="3d"]')!;
    this.btnReframe = host.querySelector('[data-a="reencuadrar"]')!;
    this.seg2d.addEventListener('click', () => this.setView('2d'));
    this.seg3d.addEventListener('click', () => this.setView('3d'));
    this.btnReframe.addEventListener('click', () => {
      this.cam = { ...CAM_START };
      this.schedule();
    });
    host.querySelector('[data-a="centrar"]')?.addEventListener('click', () => opts.onRecenter?.());
    this.bindCamera();
    new ResizeObserver(() => this.schedule()).observe(host.querySelector('.scene-canvas')!);
    document.addEventListener('themechange', () => this.schedule());
    this.setView(this.view);
  }

  setPose(p: Pose | null) {
    this.pose = p;
    this.schedule();
  }

  private setView(v: '2d' | '3d') {
    this.view = v;
    store('escena.vista', v);
    this.seg2d.setAttribute('aria-pressed', String(v === '2d'));
    this.seg3d.setAttribute('aria-pressed', String(v === '3d'));
    this.btnReframe.hidden = v !== '3d';
    this.canvas.classList.toggle('orbit', v === '3d');
    this.schedule();
  }

  private bindCamera() {
    let drag: { x: number; y: number; az: number; el: number } | null = null;
    this.canvas.addEventListener('pointerdown', (e) => {
      if (this.view !== '3d') return;
      drag = { x: e.clientX, y: e.clientY, az: this.cam.az, el: this.cam.el };
      this.canvas.setPointerCapture(e.pointerId);
    });
    this.canvas.addEventListener('pointermove', (e) => {
      if (!drag) return;
      this.cam.az = drag.az + (e.clientX - drag.x) * 0.008;
      this.cam.el = Math.min(1.25, Math.max(0.07, drag.el + (e.clientY - drag.y) * 0.006));
      this.schedule();
    });
    const end = () => (drag = null);
    this.canvas.addEventListener('pointerup', end);
    this.canvas.addEventListener('pointercancel', end);
    this.canvas.addEventListener(
      'wheel',
      (e) => {
        if (this.view !== '3d') return;
        e.preventDefault();
        this.cam.zoom = Math.min(3, Math.max(0.5, this.cam.zoom * Math.exp(-e.deltaY * 0.0015)));
        this.schedule();
      },
      { passive: false },
    );
  }

  private schedule() {
    if (this.req) return;
    this.req = requestAnimationFrame(() => {
      this.req = 0;
      this.draw();
    });
  }

  // ------------------------------------------------------------ dibujo
  private draw() {
    const box = this.canvas.parentElement!;
    const dpr = Math.min(2, devicePixelRatio || 1);
    const W = Math.max(120, box.clientWidth);
    const H = Math.max(120, box.clientHeight);
    if (this.canvas.width !== Math.round(W * dpr) || this.canvas.height !== Math.round(H * dpr)) {
      this.canvas.width = Math.round(W * dpr);
      this.canvas.height = Math.round(H * dpr);
    }
    this.W = W;
    this.H = H;
    const g = this.canvas.getContext('2d')!;
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.fillStyle = cssVar('--scene-sky');
    g.fillRect(0, 0, W, H);
    const p = this.pose ?? { th: 0, thRef: 0, x: 0, phiL: 0, phiR: 0, u: 0, angDeg: NaN, t: 0, st: 'IDLE' as const };
    if (this.view === '3d') this.draw3D(g, p);
    else this.draw2D(g, p);
    this.readout(g, p);
  }

  private readout(g: CanvasRenderingContext2D, p: Pose) {
    const W = this.W;
    g.textAlign = 'right';
    g.font = '600 26px "JetBrains Mono", monospace';
    g.fillStyle = p.st === 'ACTIVE' ? cssVar('--ink') : cssVar('--rojo');
    g.fillText(isFinite(p.angDeg) ? `${p.angDeg.toFixed(1)}°` : '—', W - 12, 34);
    g.font = '10px "JetBrains Mono", monospace';
    g.fillStyle = cssVar('--ink-3');
    g.fillText('ÁNGULO θ (robot)', W - 12, 48);
    if (this.pose) {
      g.font = '600 13px "JetBrains Mono", monospace';
      g.fillStyle = cssVar('--ink-2');
      g.fillText(`t = ${p.t.toFixed(2)} s`, W - 12, 68);
    }
    g.textAlign = 'left';
  }

  private draw2D(g: CanvasRenderingContext2D, p: Pose) {
    const { W, H } = this;
    const floor = H - 38;
    const tall = TOP_HEIGHT + R + 0.03;
    const S = Math.max(150, Math.min((floor - 70) / tall, (0.5 * W) / 0.11));
    const cx = W * 0.5;

    g.fillStyle = cssVar('--scene-floor');
    g.fillRect(0, floor, W, H - floor);
    g.strokeStyle = cssVar('--scene-line');
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(0, floor + 0.5);
    g.lineTo(W, floor + 0.5);
    g.stroke();

    // regla del piso cada 5 cm, arrastrada por la posición del eje
    const step = 0.05 * S;
    const off = (-p.x * S) % step;
    g.font = '10px "JetBrains Mono", monospace';
    g.textAlign = 'center';
    g.fillStyle = cssVar('--ink-3');
    const i0 = Math.floor((0 - cx - off) / step);
    const i1 = Math.ceil((W - cx - off) / step);
    for (let i = i0; i <= i1; i++) {
      const px = cx + off + i * step;
      const xm = (px - cx) / S + p.x;
      const zero = Math.abs(xm) < 0.001;
      g.strokeStyle = zero ? cssVar('--rojo') : cssVar('--scene-line');
      g.beginPath();
      g.moveTo(px, floor);
      g.lineTo(px, floor + (zero ? 14 : 7));
      g.stroke();
      if (Math.abs(Math.round(xm * 100) % 10) < 1) g.fillText(`${xm >= 0 ? '+' : ''}${(xm * 100).toFixed(0)}`, px, floor + 25);
    }
    g.textAlign = 'left';
    g.fillText('posición del eje [cm]', 10, H - 6);

    const axleY = floor - R * S;
    const rp = R * S;
    g.save();
    g.translate(cx, axleY);

    // rueda (disco con cuatro rayos). En el lienzo y crece hacia abajo: un ángulo
    // creciente es horario, que es como gira la rueda al avanzar hacia la derecha
    g.beginPath();
    g.arc(0, 0, rp, 0, Math.PI * 2);
    g.fillStyle = cssVar('--surface-2');
    g.fill();
    g.lineWidth = Math.max(2, rp * 0.13);
    g.strokeStyle = cssVar('--chasis');
    g.stroke();
    g.lineWidth = 1.4;
    g.strokeStyle = cssVar('--scene-line');
    for (let k = 0; k < 4; k++) {
      const a = p.phiR + (k * Math.PI) / 2;
      g.beginPath();
      g.moveTo(0, 0);
      g.lineTo(rp * 0.82 * Math.cos(a), rp * 0.82 * Math.sin(a));
      g.stroke();
    }

    // vertical y ángulo deseado
    const refLen = Math.max(0.1, COM_HEIGHT * 1.9) * S;
    g.setLineDash([3, 4]);
    g.strokeStyle = cssVar('--scene-line');
    g.lineWidth = 1;
    g.beginPath();
    g.moveTo(0, 0);
    g.lineTo(0, -refLen);
    g.stroke();
    g.strokeStyle = cssVar('--ambar');
    g.lineWidth = 1.5;
    g.beginPath();
    g.moveTo(0, 0);
    g.lineTo(refLen * Math.sin(p.thRef), -refLen * Math.cos(p.thRef));
    g.stroke();
    g.setLineDash([]);
    g.fillStyle = cssVar('--ambar');
    g.font = '10px "JetBrains Mono", monospace';
    g.fillText('θ ref', refLen * Math.sin(p.thRef) + 4, -refLen * Math.cos(p.thRef));

    // cuerpo inclinado: rotar +θ inclina hacia la derecha (adelante)
    g.save();
    g.rotate(p.th);
    g.strokeStyle = cssVar('--chasis');
    g.lineWidth = Math.max(2, 0.008 * S);
    g.beginPath();
    g.moveTo(0, 0);
    g.lineTo(0, -TOP_HEIGHT * S);
    g.stroke();
    for (const b of BLOCKS) {
      const w = b.w * S;
      const a = Math.max(3, b.a * S);
      g.fillStyle = b.color;
      g.beginPath();
      g.roundRect(-w / 2, -b.h * S - a / 2, w, a, Math.min(3, a / 2));
      g.fill();
      g.strokeStyle = 'rgba(0,0,0,.22)';
      g.lineWidth = 1;
      g.stroke();
    }
    const cy = -COM_HEIGHT * S;
    g.beginPath();
    g.arc(0, cy, Math.max(5, 0.009 * S), 0, Math.PI * 2);
    g.fillStyle = cssVar('--dorado');
    g.fill();
    g.strokeStyle = cssVar('--ink');
    g.lineWidth = 1.2;
    g.stroke();
    g.beginPath();
    g.moveTo(-6, cy);
    g.lineTo(6, cy);
    g.moveTo(0, cy - 6);
    g.lineTo(0, cy + 6);
    g.stroke();
    g.restore();

    // arco del ángulo
    if (Math.abs(p.th) > 0.004) {
      const Ra = Math.max(26, 0.055 * S);
      g.strokeStyle = cssVar('--rojo');
      g.lineWidth = 1.6;
      g.beginPath();
      g.arc(0, 0, Ra, -Math.PI / 2, -Math.PI / 2 + p.th, p.th < 0);
      g.stroke();
    }
    this.pwmArrow2D(g, p.u, rp);
    g.restore();
  }

  /** Arco con flecha sobre la rueda: sentido y magnitud del PWM aplicado. */
  private pwmArrow2D(g: CanvasRenderingContext2D, u: number, rp: number) {
    if (Math.abs(u) < 0.5) return;
    const frac = Math.min(1, Math.abs(u) / 255);
    const sgn = Math.sign(u);
    const Rr = rp * 1.28;
    const a0 = -Math.PI / 2;
    const a1 = a0 + sgn * (0.5 + 1.9 * frac);
    g.strokeStyle = cssVar('--series-1');
    g.lineWidth = Math.max(2.2, rp * 0.09);
    g.lineCap = 'round';
    g.beginPath();
    g.arc(0, 0, Rr, a0, a1, sgn < 0);
    g.stroke();
    const ex = Rr * Math.cos(a1);
    const ey = Rr * Math.sin(a1);
    const ta = a1 + (sgn * Math.PI) / 2;
    const hl = Math.max(6, rp * 0.3);
    const ab = 0.42;
    g.beginPath();
    g.moveTo(ex + hl * Math.cos(ta + Math.PI - ab), ey + hl * Math.sin(ta + Math.PI - ab));
    g.lineTo(ex, ey);
    g.lineTo(ex + hl * Math.cos(ta + Math.PI + ab), ey + hl * Math.sin(ta + Math.PI + ab));
    g.lineWidth = Math.max(1.8, rp * 0.07);
    g.stroke();
    g.lineCap = 'butt';
  }

  // ------------------------------------------------------------ 3D
  private draw3D(g: CanvasRenderingContext2D, p: Pose) {
    const { W, H } = this;
    const tall = TOP_HEIGHT + R;
    const wide = Math.max(TRACK, ...BLOCKS.map((b) => Math.max(b.w, b.d)));
    const size = Math.max(tall, 0.8 * wide);
    const fov = (30 * Math.PI) / 180;
    // encuadre por el lado menor: en una escena alta y angosta el robot no se sale de los costados
    const focal = Math.min(H, W * 1.15) / 2 / Math.tan(fov / 2);
    const dist = (2.75 * Math.max(0.16, size)) / this.cam.zoom;
    const T: V3 = [0, size * 0.5, 0];
    const C: V3 = [
      T[0] + dist * Math.cos(this.cam.el) * Math.cos(this.cam.az),
      T[1] + dist * Math.sin(this.cam.el),
      T[2] + dist * Math.cos(this.cam.el) * Math.sin(this.cam.az),
    ];
    const f = n3(sub3(T, C));
    const rgt = n3(cross3(f, [0, 1, 0]));
    const up = cross3(rgt, f);
    const proj = (q: V3) => {
      const v = sub3(q, C);
      const z = dot3(v, f);
      if (z < 1e-4) return null;
      const k = focal / z;
      return { x: W / 2 + dot3(v, rgt) * k, y: H * 0.54 - dot3(v, up) * k, z };
    };
    const line = (a: V3, b: V3, col: string, width = 1, dash?: number[]) => {
      const pa = proj(a);
      const pb = proj(b);
      if (!pa || !pb) return;
      g.strokeStyle = col;
      g.lineWidth = width;
      if (dash) g.setLineDash(dash);
      g.beginPath();
      g.moveTo(pa.x, pa.y);
      g.lineTo(pb.x, pb.y);
      g.stroke();
      g.setLineDash([]);
    };
    const poly = (pts: V3[], fill: string) => {
      const q = pts.map(proj);
      if (q.some((x) => !x)) return;
      g.beginPath();
      q.forEach((x, i) => (i ? g.lineTo(x!.x, x!.y) : g.moveTo(x!.x, x!.y)));
      g.closePath();
      g.fillStyle = fill;
      g.fill();
    };

    // piso con marcas cada 5 cm que se desplazan con el robot
    const zB = 0.42 * dist;
    const x1 = 0.62 * dist;
    const x0 = -x1;
    const lineCol = cssVar('--scene-line');
    poly([[x0, 0, -zB], [x1, 0, -zB], [x1, 0, zB], [x0, 0, zB]], cssVar('--scene-floor'));
    const shift = -p.x % 0.05;
    for (let i = Math.ceil((x0 - shift) / 0.05); i <= Math.floor((x1 - shift) / 0.05); i++) {
      const xw = shift + i * 0.05;
      const zero = Math.abs(xw + p.x) < 0.002;
      line([xw, 0, -zB], [xw, 0, zB], zero ? cssVar('--rojo') : lineCol, zero ? 1.6 : 1);
    }
    poly([[-R * 1.05, 0.001, -(TRACK / 2 + 0.022)], [R * 1.05, 0.001, -(TRACK / 2 + 0.022)], [R * 1.05, 0.001, TRACK / 2 + 0.022], [-R * 1.05, 0.001, TRACK / 2 + 0.022]], 'rgba(0,0,0,.10)');

    // sólidos, de lejos a cerca (algoritmo del pintor)
    const ct = Math.cos(p.th);
    const st = Math.sin(p.th);
    const ex: V3 = [ct, -st, 0];
    const ey: V3 = [st, ct, 0];
    const ez: V3 = [0, 0, 1];
    const bodyPoint = (b: V3): V3 => [ex[0] * b[0] + ey[0] * b[1], ex[1] * b[0] + ey[1] * b[1] + R, b[2]];
    const faces: Face[] = [];
    const box = (c: V3, hw: number, hh: number, hd: number, col: string) => {
      const V = (i: number, j: number, k: number): V3 => [
        c[0] + ex[0] * i * hw + ey[0] * j * hh,
        c[1] + ex[1] * i * hw + ey[1] * j * hh,
        c[2] + k * hd,
      ];
      const F = (pts: V3[], n: V3) => faces.push({ p: pts, n, col, edge: true });
      F([V(1, -1, -1), V(1, 1, -1), V(1, 1, 1), V(1, -1, 1)], ex);
      F([V(-1, -1, -1), V(-1, -1, 1), V(-1, 1, 1), V(-1, 1, -1)], neg3(ex));
      F([V(-1, 1, -1), V(-1, 1, 1), V(1, 1, 1), V(1, 1, -1)], ey);
      F([V(-1, -1, -1), V(1, -1, -1), V(1, -1, 1), V(-1, -1, 1)], neg3(ey));
      F([V(-1, -1, 1), V(1, -1, 1), V(1, 1, 1), V(-1, 1, 1)], ez);
      F([V(-1, -1, -1), V(-1, 1, -1), V(1, 1, -1), V(1, -1, -1)], neg3(ez));
    };
    for (const b of BLOCKS as Block[]) box(bodyPoint([0, b.h, 0]), b.w / 2, Math.max(0.0025, b.a / 2), b.d / 2, b.color);
    box(bodyPoint([0, TOP_HEIGHT / 2, 0]), 0.005, TOP_HEIGHT / 2, 0.005, cssVar('--chasis'));
    const chasis = cssVar('--chasis');
    for (const s of [-1, 1]) {
      const N = 22;
      const zc = (s * TRACK) / 2;
      const z0 = zc - 0.0065;
      const z1 = zc + 0.0065;
      const pt = (a: number, z: number): V3 => [R * Math.cos(a), R + R * Math.sin(a), z];
      for (let i = 0; i < N; i++) {
        const a0 = (i / N) * 2 * Math.PI;
        const a1 = ((i + 1) / N) * 2 * Math.PI;
        const nm: V3 = [Math.cos((a0 + a1) / 2), Math.sin((a0 + a1) / 2), 0];
        faces.push({ p: [pt(a0, z0), pt(a1, z0), pt(a1, z1), pt(a0, z1)], n: nm, col: chasis, edge: true });
        faces.push({ p: [[0, R, z1], pt(a0, z1), pt(a1, z1)], n: [0, 0, 1], col: chasis });
        faces.push({ p: [[0, R, z0], pt(a0, z0), pt(a1, z0)], n: [0, 0, -1], col: chasis });
      }
    }
    const visible: { q: { x: number; y: number }[]; col: string; z: number; edge: boolean }[] = [];
    for (const fc of faces) {
      const ctr: V3 = [0, 0, 0];
      for (const q of fc.p) for (let k = 0; k < 3; k++) ctr[k] += q[k] / fc.p.length;
      if (dot3(fc.n, sub3(ctr, C)) >= 0) continue; // cara de espaldas
      const q = fc.p.map(proj);
      if (q.some((x) => !x)) continue;
      visible.push({ q: q as { x: number; y: number }[], col: shade(fc.col, fc.n), z: dot3(sub3(ctr, C), f), edge: !!fc.edge });
    }
    visible.sort((a, b) => b.z - a.z);
    for (const fc of visible) {
      g.beginPath();
      fc.q.forEach((x, i) => (i ? g.lineTo(x.x, x.y) : g.moveTo(x.x, x.y)));
      g.closePath();
      g.fillStyle = fc.col;
      g.fill();
      if (fc.edge) {
        g.strokeStyle = 'rgba(0,0,0,.16)';
        g.lineWidth = 0.6;
        g.stroke();
      }
    }

    // rayos de la rueda del lado de la cámara, con su propio giro
    const side = C[2] >= 0 ? 1 : -1;
    const zf = side * (TRACK / 2 + 0.0068);
    const phi = side > 0 ? p.phiR : p.phiL;
    for (let k = 0; k < 4; k++) {
      const a = -phi + (k * Math.PI) / 2;
      line([0, R, zf], [R * 0.86 * Math.cos(a), R + R * 0.86 * Math.sin(a), zf], lineCol, 1.4);
    }

    // vertical, ángulo deseado y centro de masa
    const refLen = Math.max(0.1, COM_HEIGHT * 1.9);
    line([0, R, 0], [0, R + refLen, 0], lineCol, 1, [3, 4]);
    line([0, R, 0], [refLen * Math.sin(p.thRef), R + refLen * Math.cos(p.thRef), 0], cssVar('--ambar'), 1.5, [3, 4]);
    const pcm = proj(bodyPoint([0, COM_HEIGHT, 0]));
    if (pcm) {
      g.beginPath();
      g.arc(pcm.x, pcm.y, 5.5, 0, Math.PI * 2);
      g.fillStyle = cssVar('--dorado');
      g.fill();
      g.strokeStyle = cssVar('--ink');
      g.lineWidth = 1.2;
      g.stroke();
    }

    // PWM: arco con flecha en el plano de la rueda cercana
    if (Math.abs(p.u) >= 0.5) {
      const frac = Math.min(1, Math.abs(p.u) / 255);
      const sg = Math.sign(p.u);
      const Rr = R * 1.32;
      const zc = side * (TRACK / 2 + 0.016);
      const a0 = Math.PI / 2;
      const a1 = a0 - sg * (0.5 + 1.9 * frac);
      const pts = [];
      for (let i = 0; i <= 24; i++) {
        const a = a0 + ((a1 - a0) * i) / 24;
        pts.push(proj([Rr * Math.cos(a), R + Rr * Math.sin(a), zc]));
      }
      if (pts.every((x) => x)) {
        g.strokeStyle = cssVar('--series-1');
        g.lineWidth = 3;
        g.lineCap = 'round';
        g.beginPath();
        pts.forEach((x, i) => (i ? g.lineTo(x!.x, x!.y) : g.moveTo(x!.x, x!.y)));
        g.stroke();
        const u = pts[24]!;
        const v = pts[23]!;
        const ang = Math.atan2(u.y - v.y, u.x - v.x);
        g.beginPath();
        g.moveTo(u.x - 9 * Math.cos(ang - 0.45), u.y - 9 * Math.sin(ang - 0.45));
        g.lineTo(u.x, u.y);
        g.lineTo(u.x - 9 * Math.cos(ang + 0.45), u.y - 9 * Math.sin(ang + 0.45));
        g.lineWidth = 2.2;
        g.stroke();
        g.lineCap = 'butt';
      }
    }
    g.fillStyle = cssVar('--ink-3');
    g.font = '10px "JetBrains Mono", monospace';
    g.fillText(`eje en x = ${p.x >= 0 ? '+' : ''}${(p.x * 100).toFixed(1)} cm · marcas cada 5 cm`, 10, H - 8);
  }
}
