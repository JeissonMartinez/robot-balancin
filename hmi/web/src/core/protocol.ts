/**
 * Tipos de los mensajes gateway ↔ HMI (docs/hmi/GATEWAY.md). Las tramas de
 * telemetría y los parámetros son los del firmware (docs/hmi/PROTOCOLO.md).
 */

export type RobotState = 'ACTIVE' | 'IDLE' | 'ESTOP' | 'BENCH';

/** Trama de telemetría del robot (sin el campo "type"). */
export interface Frame {
  seq: number;
  t: number; // ms del robot
  ang: number;
  ref: number;
  w: number;
  pwm: number;
  pwmM: number;
  rpmL: number;
  rpmR: number;
  kp: number;
  dt: number;
  uP?: number; // desde fw 0.3.0
  uI?: number;
  uD?: number;
  st: RobotState;
}

export type FrameKey = Exclude<keyof Frame, 'st'>;

export type CapState = 'none' | 'running' | 'done' | 'aborted';

/** Trama del banco de motores (fw ≥ 0.6.0), sin el campo "type". */
export interface MotFrame {
  seq: number;
  t: number; // ms del robot
  pwmL: number;
  pwmR: number;
  cL: number; // cuentas acumuladas
  cR: number;
  rpmL: number; // RPM de rueda en los últimos 20 ms
  rpmR: number;
  eLA: number; // flancos por canal desde que se entró al banco
  eLB: number;
  eRA: number;
  eRB: number;
  lv: number; // nivel de los pines: bit0 L_A, bit1 L_B, bit2 R_A, bit3 R_B
  es: number; // 1 = parada de emergencia activa
  cs: CapState; // estado del ensayo
  ci: number; // id del ensayo en el robot
  ck: number; // muestras tomadas
  cn: number; // muestras previstas
}

/** Datos del encoder que el robot informa en "hello". */
export interface EncInfo {
  ppr: number; // pulsos por canal por vuelta del motor
  decode: number; // cuentas por pulso (4)
  gear: number; // reducción
  cpw: number; // cuentas por vuelta de rueda
}

/** Estado del banco (respuesta a "bench"). */
export interface BenchStatus {
  on: boolean;
  estop: boolean;
  pwm: [number, number];
  max_samples: number;
  cap: { id: number; st: CapState; n: number; total: number; period_ms?: number; motor?: string; seq?: [number, number][] };
}

/** Ensayo guardado en la base del PC. `rows`: [t_us, pwmL, pwmR, cL, cR]. */
export interface Capture {
  id: number;
  session_id: number | null;
  created_at: string;
  fw: string | null;
  cap_id: number;
  state: CapState;
  motor: 'L' | 'R' | 'LR';
  period_ms: number;
  seq: [number, number][];
  enc: EncInfo | null;
  n: number;
  notes: string | null;
  rows?: [number, number, number, number, number][];
}

export interface Status {
  state: 'disconnected' | 'connecting' | 'connected';
  error: string | null;
  transport: string | null;
  target: string | null;
  fw: string | null;
  proto: number | null;
  params_src: 'nvs' | 'factory' | null;
  session_id: number | null;
  robot_state: RobotState | null;
  rate: number;
  gaps: number;
  clients: number;
  retrying?: boolean; // WiFi: reintentando tras perder la conexión
  enc?: EncInfo | null; // fw ≥ 0.6.0
  bench?: boolean; // el firmware tiene banco de motores
}

export interface ParamDesc {
  key: string;
  type: 'float' | 'int' | 'bool' | 'enum' | 'sign';
  min?: number;
  max?: number;
  options?: string[];
  def: number | boolean | string;
  unit: string;
  desc: string;
}

export type Params = Record<string, number | boolean | string>;

export interface GwEvent {
  kind: 'connect' | 'disconnect' | 'reboot' | 'connection_lost' | 'cmd' | 'log' | string;
  t_ms: number | null;
  host_ts: number;
  payload: any;
}

export interface Ack {
  ok: boolean;
  err?: string;
  params?: Params;
  [k: string]: unknown;
}

/** Juego de parámetros guardado en la base del PC. */
export interface ParamSet {
  id: number;
  name: string;
  created_at: string;
  notes: string | null;
  fw: string | null;
  params: Params;
}

/** Sesión guardada en la base del PC. */
export interface Session {
  id: number;
  started_at: string;
  ended_at: string | null;
  transport: string;
  target: string | null;
  fw: string | null;
  proto: number | null;
  params: Params | null;
  notes: string | null;
  frames: number;
  t_first: number | null;
  t_last: number | null;
  n_events: number;
}

/** Fila de la tabla telemetry (nombres de columna de la base). */
export interface TelemetryRow {
  session_id: number;
  host_ts: number;
  seq: number;
  t_ms: number;
  ang: number; ref: number; w: number; pwm: number; pwm_m: number; rpm_l: number; rpm_r: number;
  kp: number; dt: number; u_p: number | null; u_i: number | null; u_d: number | null; st: string;
}

export interface SessionColumns {
  total: number;
  step: number;
  columns: Record<string, (number | string | null)[]>;
}

export type ServerMsg =
  | { type: 'snapshot'; status: Status; schema: ParamDesc[] | null; params: Params | null }
  | { type: 'status'; status: Status }
  | { type: 'tel'; frames: Frame[] }
  | { type: 'mot'; frames: MotFrame[] }
  | { type: 'captures_changed'; id: number }
  | { type: 'params'; params: Params }
  | { type: 'event'; event: GwEvent }
  | { type: 'param_sets_changed' }
  | { type: 'sessions_changed' }
  | ({ type: 'ack'; id: number } & Ack);
