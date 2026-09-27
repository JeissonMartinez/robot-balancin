/**
 * Estado que dibuja la escena, calculado a partir de la telemetría.
 *
 * La telemetría no trae posición: se integra la velocidad de las ruedas. El avance usa
 * la media de las dos ruedas (el encoder izquierdo está en revisión: si falla, la
 * posición deriva). Cada rueda gira con su propia RPM.
 *
 * Convenios: en el firmware ángulo < 0 = inclinado hacia adelante; en la escena,
 * como en el simulador, θ > 0 es hacia adelante (+x, a la derecha en la vista lateral).
 */
import type { Frame } from '../core/protocol';
import { WHEEL_RADIUS, rpmToSpeed } from './geometry';

export interface Pose {
  th: number; // rad, + adelante
  thRef: number; // rad
  x: number; // m, posición del eje
  phiL: number; // rad, giro acumulado de cada rueda
  phiR: number;
  u: number; // PWM aplicado al motor (−255..255)
  angDeg: number; // ángulo tal como lo reporta el robot
  t: number; // s
  st: Frame['st'];
}

const rpmToRad = (rpm: number) => (rpm / 60) * 2 * Math.PI;

/** Integra tramas en orden y entrega la pose de la última. */
export class PoseIntegrator {
  private x = 0;
  private phiL = 0;
  private phiR = 0;
  private lastT: number | null = null;
  pose: Pose | null = null;

  reset() {
    this.x = this.phiL = this.phiR = 0;
    this.lastT = null;
    this.pose = null;
  }

  /** Vuelve a poner el eje en x = 0 sin perder el resto. */
  recenter() {
    this.x = 0;
    if (this.pose) this.pose = { ...this.pose, x: 0 };
  }

  push(f: Frame) {
    const t = f.t / 1000;
    const dt = this.lastT === null ? 0 : Math.min(0.2, Math.max(0, t - this.lastT));
    this.lastT = t;
    const rpmL = f.rpmL ?? 0;
    const rpmR = f.rpmR ?? 0;
    this.x += rpmToSpeed((rpmL + rpmR) / 2) * dt;
    this.phiL += rpmToRad(rpmL) * dt;
    this.phiR += rpmToRad(rpmR) * dt;
    this.pose = {
      th: (-f.ang * Math.PI) / 180,
      thRef: (-f.ref * Math.PI) / 180,
      x: this.x,
      phiL: this.phiL,
      phiR: this.phiR,
      u: f.pwmM ?? f.pwm ?? 0,
      angDeg: f.ang,
      t,
      st: f.st,
    };
  }
}

/** Poses de una sesión completa (para reproducirla). */
export function posesFromColumns(cols: Record<string, (number | string | null)[]>): Pose[] {
  const n = cols.t_ms?.length ?? 0;
  const integ = new PoseIntegrator();
  const out: Pose[] = [];
  const num = (k: string, i: number) => (cols[k]?.[i] as number | null) ?? 0;
  for (let i = 0; i < n; i++) {
    integ.push({
      seq: num('seq', i), t: num('t_ms', i), ang: num('ang', i), ref: num('ref', i), w: num('w', i),
      pwm: num('pwm', i), pwmM: num('pwm_m', i), rpmL: num('rpm_l', i), rpmR: num('rpm_r', i),
      kp: num('kp', i), dt: num('dt', i), st: (cols.st?.[i] as Frame['st']) ?? 'IDLE',
    });
    out.push(integ.pose!);
  }
  return out;
}

export { WHEEL_RADIUS };
