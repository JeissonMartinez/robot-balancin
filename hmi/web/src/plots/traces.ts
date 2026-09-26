/**
 * Trazas en vivo con uPlot.
 *
 * - Un gráfico por magnitud (nunca dos ejes y): ángulo, velocidad angular, control, ruedas.
 * - Búfer circular de 60 s (3000 tramas a 50 Hz); se dibuja la ventana elegida.
 * - Cursor sincronizado entre gráficos. En pausa: arrastrar acerca todos a la vez,
 *   doble clic vuelve a la ventana.
 * - Marcas verticales en los eventos (cambios de parámetros, parada, reinicio), en el
 *   tiempo del robot en que ocurrieron.
 * - Colores de las series por orden fijo (--series-1..5, ver tokens.css); los
 *   gráficos se reconstruyen al cambiar el tema.
 */
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import type { Frame, FrameKey } from '../core/protocol';
import { cssVar } from '../ui/theme';

interface SeriesDef {
  key: FrameKey;
  label: string;
  slot: 1 | 2 | 3 | 4 | 5;
  dash?: number[];
  show?: boolean;
  decimals: number;
}
interface PlotDef {
  id: string;
  title: string;
  unit: string;
  series: SeriesDef[];
}

export const PLOTS: PlotDef[] = [
  {
    id: 'ang', title: 'Ángulo', unit: '°',
    series: [
      { key: 'ang', label: 'θ', slot: 1, decimals: 2 },
      { key: 'ref', label: 'θ ref', slot: 2, dash: [6, 4], decimals: 2 },
    ],
  },
  {
    id: 'w', title: 'Velocidad angular', unit: '°/s',
    series: [{ key: 'w', label: 'ω', slot: 1, decimals: 1 }],
  },
  {
    id: 'u', title: 'Control', unit: 'PWM',
    series: [
      { key: 'pwm', label: 'u', slot: 1, decimals: 1 },
      { key: 'pwmM', label: 'u motor', slot: 2, dash: [5, 3], show: false, decimals: 1 },
      { key: 'uP', label: 'P', slot: 3, decimals: 1 },
      { key: 'uI', label: 'I', slot: 4, show: false, decimals: 1 },
      { key: 'uD', label: 'D', slot: 5, decimals: 1 },
    ],
  },
  {
    id: 'rpm', title: 'Ruedas', unit: 'RPM',
    series: [
      { key: 'rpmR', label: 'derecha', slot: 1, decimals: 1 },
      { key: 'rpmL', label: 'izquierda (encoder defectuoso)', slot: 2, show: false, decimals: 1 },
    ],
  },
];

const RATE_HZ = 50;
const MAX_SECONDS = 60;
const CAPACITY = RATE_HZ * MAX_SECONDS;

interface Marker {
  t: number; // s, tiempo del robot
  label: string;
}

type Col = (number | null)[];

export class Traces {
  windowS = 10;
  private paused = false;
  private t: number[] = [];
  private cols = new Map<FrameKey, Col>();
  private markers: Marker[] = [];
  private charts: { def: PlotDef; u: uPlot; host: HTMLElement }[] = [];
  private visibility = new Map<string, boolean>();
  private frameReq = 0;

  constructor(private root: HTMLElement) {
    for (const p of PLOTS) for (const s of p.series) {
      this.cols.set(s.key, []);
      this.visibility.set(s.key, s.show !== false);
    }
    this.build();
    new ResizeObserver(() => this.resize()).observe(root);
    document.addEventListener('themechange', () => this.build());
  }

  // ------------------------------------------------------------ datos
  push(frames: Frame[]) {
    for (const f of frames) {
      this.t.push(f.t / 1000);
      for (const [key, col] of this.cols) {
        const v = f[key];
        col.push(typeof v === 'number' ? v : null);
      }
    }
    if (this.t.length > CAPACITY + 250) {
      const cut = this.t.length - CAPACITY;
      this.t.splice(0, cut);
      for (const col of this.cols.values()) col.splice(0, cut);
      this.markers = this.markers.filter((m) => m.t >= this.t[0]);
    }
    this.schedule();
  }

  /** Nueva sesión: el tiempo del robot vuelve a empezar. */
  reset() {
    this.t = [];
    for (const col of this.cols.values()) col.length = 0;
    this.markers = [];
    this.schedule(true);
  }

  addMarker(tMs: number, label: string) {
    this.markers.push({ t: tMs / 1000, label });
    this.schedule(true);
  }

  setWindow(seconds: number) {
    this.windowS = seconds;
    this.schedule(true);
  }

  setPaused(p: boolean) {
    this.paused = p;
    if (!p) this.schedule(true);
  }

  // ------------------------------------------------------------ dibujo
  private schedule(force = false) {
    if ((this.paused && !force) || this.frameReq) return;
    this.frameReq = requestAnimationFrame(() => {
      this.frameReq = 0;
      this.render();
    });
  }

  private render() {
    const n = this.t.length;
    const tMax = n ? this.t[n - 1] : this.windowS;
    const tMin = tMax - this.windowS;
    // búsqueda binaria del primer punto dentro de la ventana
    let lo = 0;
    let hi = n;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (this.t[mid] < tMin) lo = mid + 1;
      else hi = mid;
    }
    const i = lo;
    const tSlice = this.t.slice(i);
    for (const { def, u } of this.charts) {
      const data = [tSlice, ...def.series.map((s) => this.cols.get(s.key)!.slice(i))] as uPlot.AlignedData;
      u.batch(() => {
        u.setData(data, true);
        u.setScale('x', { min: tMin, max: tMax });
      });
    }
  }

  private size() {
    const w = Math.max(240, this.root.clientWidth - 8);
    return { width: w, height: w < 600 ? 120 : 150 };
  }

  private resize() {
    const s = this.size();
    for (const { u } of this.charts) u.setSize(s);
  }

  private build() {
    for (const { u } of this.charts) {
      u.series.forEach((s, k) => {
        if (k > 0) this.visibility.set((s as uPlot.Series & { key: string }).key, s.show !== false);
      });
      u.destroy();
    }
    this.charts = [];
    this.root.innerHTML = '';

    const ink3 = cssVar('--ink-3');
    const grid = cssVar('--line-soft');
    const tick = cssVar('--line');
    const markerColor = cssVar('--marker');
    const font = '11px "JetBrains Mono", Menlo, monospace';

    const zoomAll = (from: uPlot) => {
      if (from.select.width < 4 || !this.paused) {
        from.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
        return;
      }
      const min = from.posToVal(from.select.left, 'x');
      const max = from.posToVal(from.select.left + from.select.width, 'x');
      for (const { u } of this.charts) {
        u.setScale('x', { min, max });
        u.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
      }
    };

    PLOTS.forEach((def, idx) => {
      const host = document.createElement('div');
      host.className = 'plot';
      host.innerHTML = `<div class="plot-title">${def.title}<span class="u">[${def.unit}]</span></div>`;
      this.root.append(host);

      const opts: uPlot.Options = {
        ...this.size(),
        cursor: {
          sync: { key: 'trazas' },
          drag: { x: true, y: false, setScale: false },
        },
        scales: { x: { time: false, auto: false } },
        legend: { live: true },
        series: [
          { label: 't', value: (_u, v) => (v == null ? '—' : `${v.toFixed(2)} s`) },
          ...def.series.map((s) => ({
            key: s.key,
            label: s.label,
            stroke: cssVar(`--series-${s.slot}`),
            width: 2,
            dash: s.dash,
            show: this.visibility.get(s.key),
            spanGaps: false,
            points: { show: false },
            value: (_u: uPlot, v: number | null) => (v == null ? '—' : v.toFixed(s.decimals)),
          })),
        ],
        axes: [
          {
            stroke: ink3, font, size: idx === PLOTS.length - 1 ? 32 : 22,
            grid: { stroke: grid, width: 1 }, ticks: { stroke: tick, width: 1, size: 4 },
            values: idx === PLOTS.length - 1 ? undefined : () => [],
          },
          { stroke: ink3, font, size: 50, grid: { stroke: grid, width: 1 }, ticks: { stroke: tick, width: 1, size: 4 } },
        ],
        hooks: {
          setSelect: [zoomAll],
          draw: [
            (u) => {
              const { ctx, bbox } = u;
              const xMin = u.scales.x.min!;
              const xMax = u.scales.x.max!;
              ctx.save();
              ctx.strokeStyle = markerColor;
              ctx.fillStyle = markerColor;
              ctx.lineWidth = devicePixelRatio;
              ctx.setLineDash([3 * devicePixelRatio, 3 * devicePixelRatio]);
              ctx.font = `${10 * devicePixelRatio}px "JetBrains Mono", Menlo, monospace`;
              for (const m of this.markers) {
                if (m.t < xMin || m.t > xMax) continue;
                const x = Math.round(u.valToPos(m.t, 'x', true));
                ctx.beginPath();
                ctx.moveTo(x, bbox.top);
                ctx.lineTo(x, bbox.top + bbox.height);
                ctx.stroke();
                if (idx === 0) ctx.fillText(m.label, x + 4 * devicePixelRatio, bbox.top + 11 * devicePixelRatio);
              }
              ctx.restore();
            },
          ],
        },
      };
      const u = new uPlot(opts, [[], ...def.series.map(() => [])] as uPlot.AlignedData, host);
      u.over.addEventListener('dblclick', () => setTimeout(() => this.render(), 0));
      this.charts.push({ def, u, host });
    });
    this.render();
  }
}
