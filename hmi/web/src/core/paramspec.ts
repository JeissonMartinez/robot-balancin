/**
 * Presentación de los parámetros del firmware en la HMI.
 *
 * Los rangos, tipos y valores de fábrica vienen del robot (comando `schema`); aquí sólo
 * se decide cómo mostrarlos: nombre legible, grupo, a qué estructura aplican y paso
 * del slider. Un parámetro que el firmware agregue en el futuro aparece solo, en
 * "Otros", sin tocar este archivo.
 */
import type { ParamDesc, Params } from './protocol';

type Structure = 'SpeedOuter' | 'AngleOuter';

export interface ParamUi {
  label: string;
  group: string;
  only?: Structure; // sólo tiene efecto con esa estructura
  options?: Record<number, string>; // enteros con nombre (se muestran en un selector)
}

export const GROUPS = [
  'Estructura y referencias',
  'Lazo de ángulo',
  'Lazo de velocidad',
  'Red neuronal (Kp adaptativa)',
  'Actuador y sensor',
  'Seguridad',
  'Otros',
] as const;

const UI: Record<string, ParamUi> = {
  structure: { label: 'Estructura de control', group: 'Estructura y referencias' },
  setpoint_angle: { label: 'Ángulo de referencia θ₀', group: 'Estructura y referencias' },
  speed_ref: { label: 'Velocidad deseada', group: 'Estructura y referencias', only: 'SpeedOuter' },

  kp_min: { label: 'Kp mínima', group: 'Lazo de ángulo' },
  kp_max: { label: 'Kp máxima', group: 'Lazo de ángulo' },
  kd_angle: { label: 'Kd', group: 'Lazo de ángulo' },
  ki_angle: { label: 'Ki', group: 'Lazo de ángulo', only: 'AngleOuter' },
  gyro_deriv: { label: 'Derivada con el giroscopio', group: 'Lazo de ángulo' },

  kp_v: { label: 'Kp velocidad', group: 'Lazo de velocidad', only: 'SpeedOuter' },
  ki_v: { label: 'Ki velocidad', group: 'Lazo de velocidad', only: 'SpeedOuter' },
  speed_sign: { label: 'Signo del lazo', group: 'Lazo de velocidad', only: 'SpeedOuter' },
  max_tilt_ref: { label: 'Inclinación máxima pedida', group: 'Lazo de velocidad', only: 'SpeedOuter' },
  kp_speed: { label: 'Kp velocidad (interno)', group: 'Lazo de velocidad', only: 'AngleOuter' },
  ki_speed: { label: 'Ki velocidad (interno)', group: 'Lazo de velocidad', only: 'AngleOuter' },

  nn_error_band: { label: 'Banda de error', group: 'Red neuronal (Kp adaptativa)' },
  nn_lr: { label: 'Tasa de aprendizaje', group: 'Red neuronal (Kp adaptativa)' },
  kp_tau: { label: 'Filtro de Kp (τ)', group: 'Red neuronal (Kp adaptativa)' },

  pwm_deadband: { label: 'Compensación de zona muerta', group: 'Actuador y sensor' },
  mpu_dlpf: {
    label: 'Filtro pasa-bajas del MPU',
    group: 'Actuador y sensor',
    options: { 0: '256 Hz', 1: '188 Hz', 2: '98 Hz', 3: '42 Hz', 4: '20 Hz', 5: '10 Hz', 6: '5 Hz' },
  },

  max_angle: { label: 'Ángulo de caída', group: 'Seguridad' },
  rearm_angle: { label: 'Ángulo de re-armado', group: 'Seguridad' },
};

/** Orden de presentación: el de la tabla UI; lo desconocido, al final. */
export function uiOrder(key: string): number {
  const i = Object.keys(UI).indexOf(key);
  return i < 0 ? Number.MAX_SAFE_INTEGER : i;
}

export function uiFor(d: ParamDesc): ParamUi {
  return UI[d.key] ?? { label: d.key, group: 'Otros' };
}

/** Paso "redondo" del slider: ~500 posiciones en el rango. */
export function stepFor(d: ParamDesc): number {
  if (d.type === 'int') return 1;
  const span = (d.max ?? 1) - (d.min ?? 0);
  return 10 ** Math.floor(Math.log10(span / 500));
}

export function decimalsFor(d: ParamDesc): number {
  return Math.max(0, -Math.round(Math.log10(stepFor(d))));
}

export function formatValue(d: ParamDesc, v: Params[string] | undefined): string {
  if (v === undefined) return '—';
  if (typeof v === 'number' && d.type === 'float') return v.toFixed(decimalsFor(d));
  if (typeof v === 'number' && d.type === 'int') return uiFor(d).options?.[v] ?? String(v);
  if (typeof v === 'boolean') return v ? 'sí' : 'no';
  return String(v);
}

export function sameValue(a: Params[string] | undefined, b: Params[string] | undefined): boolean {
  if (typeof a === 'number' && typeof b === 'number') return Math.abs(a - b) < 1e-6;
  return a === b;
}

/**
 * Validación local (la misma que hace el firmware), para avisar antes de enviar.
 * Devuelve un mensaje por clave con problema.
 */
export function validate(schema: ParamDesc[], values: Params): Record<string, string> {
  const errs: Record<string, string> = {};
  for (const d of schema) {
    const v = values[d.key];
    if (v === undefined) continue;
    if ((d.type === 'float' || d.type === 'int') && typeof v === 'number') {
      if (!isFinite(v)) errs[d.key] = 'no es un número';
      else if (d.min !== undefined && v < d.min) errs[d.key] = `mínimo ${d.min}`;
      else if (d.max !== undefined && v > d.max) errs[d.key] = `máximo ${d.max}`;
      else if (d.type === 'int' && !Number.isInteger(v)) errs[d.key] = 'debe ser entero';
    }
  }
  const n = (k: string) => values[k] as number | undefined;
  if (n('kp_min') !== undefined && n('kp_max') !== undefined && n('kp_min')! > n('kp_max')!) {
    errs.kp_min = errs.kp_max = 'Kp mínima debe ser ≤ Kp máxima';
  }
  if (n('rearm_angle') !== undefined && n('max_angle') !== undefined && n('rearm_angle')! >= n('max_angle')!) {
    errs.rearm_angle = errs.max_angle = 'el ángulo de re-armado debe ser menor que el de caída';
  }
  return errs;
}

/** Claves en las que `b` difiere de `a` (sólo las del schema). */
export function diffParams(schema: ParamDesc[], a: Params, b: Params): string[] {
  return schema.filter((d) => d.key in b && !sameValue(a[d.key], b[d.key])).map((d) => d.key);
}
