/**
 * Tipos de los mensajes gateway ↔ HMI (docs/hmi/GATEWAY.md). Las tramas de
 * telemetría y los parámetros son los del firmware (docs/hmi/PROTOCOLO.md).
 */

export type RobotState = 'ACTIVE' | 'IDLE' | 'ESTOP';

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

export type ServerMsg =
  | { type: 'snapshot'; status: Status; schema: ParamDesc[] | null; params: Params | null }
  | { type: 'status'; status: Status }
  | { type: 'tel'; frames: Frame[] }
  | { type: 'params'; params: Params }
  | { type: 'event'; event: GwEvent }
  | ({ type: 'ack'; id: number } & Ack);
