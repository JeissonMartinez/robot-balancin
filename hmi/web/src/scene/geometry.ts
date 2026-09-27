/**
 * Geometría del robot para dibujarlo. Sólo afecta a la escena, no a ningún cálculo
 * de control.
 *
 * Valores tomados del juego de bloques por defecto del Simulador_Balancin (diseño de
 * clase, calibrado con la sesión 1B). Para que la escena se parezca al prototipo, medir
 * el robot y corregir aquí: radio de rueda, vía y, por bloque, altura del centro sobre el
 * eje (h), largo adelante-atrás (w), espesor (a) y ancho lateral (d), todo en metros.
 */
export interface Block {
  id: string;
  name: string;
  m: number; // kg (sólo para el centro de masa dibujado)
  h: number;
  w: number;
  a: number;
  d: number;
  color: string;
}

export const WHEEL_RADIUS = 0.034; // m. Se usa también para convertir RPM en avance
export const TRACK = 0.116; // m, separación entre ruedas

export const BLOCKS: Block[] = [
  { id: 'mot', name: 'Motores y soportes', m: 0.06, h: 0.0, w: 0.1, a: 0.03, d: 0.1, color: '#6E7684' },
  { id: 'qtr', name: 'Sensor QTR y cableado', m: 0.015, h: 0.008, w: 0.08, a: 0.008, d: 0.085, color: '#1C7A5B' },
  { id: 'chi', name: 'Chasis inferior', m: 0.04, h: 0.04, w: 0.1, a: 0.006, d: 0.09, color: '#96A1B0' },
  { id: 'bat', name: 'Batería', m: 0.24, h: 0.065, w: 0.07, a: 0.022, d: 0.045, color: '#AD3332' },
  { id: 'pcb', name: 'Placa ESP32-S3 y PCB', m: 0.09, h: 0.09, w: 0.08, a: 0.015, d: 0.055, color: '#1D5FA8' },
  { id: 'imu', name: 'Sensor IMU y cableado', m: 0.08, h: 0.1, w: 0.06, a: 0.02, d: 0.04, color: '#3B2C69' },
  { id: 'chs', name: 'Chasis superior', m: 0.075, h: 0.115, w: 0.1, a: 0.006, d: 0.09, color: '#96A1B0' },
];

/** Altura del centro de masa sobre el eje de las ruedas [m]. */
export const COM_HEIGHT = BLOCKS.reduce((s, b) => s + b.m * b.h, 0) / BLOCKS.reduce((s, b) => s + b.m, 0);

/** Altura del bloque más alto sobre el eje [m]. */
export const TOP_HEIGHT = Math.max(...BLOCKS.map((b) => b.h + b.a / 2));

/** RPM de rueda → m/s de avance. */
export const rpmToSpeed = (rpm: number) => (rpm / 60) * 2 * Math.PI * WHEEL_RADIUS;
