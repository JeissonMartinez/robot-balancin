/**
 * Cliente del gateway: WebSocket /ws con reconexión automática y comandos con
 * respuesta (Promise). Toda la HMI se entera de lo que pasa por los eventos de
 * esta clase; nadie más abre sockets.
 */
import type { Ack, Frame, GwEvent, ParamDesc, Params, ServerMsg, Status } from './protocol';

interface EventMap {
  link: boolean; // WebSocket con el gateway abierto / cerrado
  status: Status;
  schema: ParamDesc[] | null;
  params: Params | null;
  frames: Frame[];
  event: GwEvent;
}
type Listener<K extends keyof EventMap> = (v: EventMap[K]) => void;

const COMMAND_TIMEOUT_MS = 45_000; // > deadband (40 s en el gateway)

export class Gateway {
  status: Status | null = null;
  schema: ParamDesc[] | null = null;
  params: Params | null = null;
  linkUp = false;

  private ws: WebSocket | null = null;
  private nextId = 1;
  private pending = new Map<number, { resolve: (a: Ack) => void; timer: number }>();
  private listeners = new Map<keyof EventMap, Set<(v: never) => void>>();
  private retry = 0;

  on<K extends keyof EventMap>(kind: K, fn: Listener<K>): () => void {
    let set = this.listeners.get(kind);
    if (!set) this.listeners.set(kind, (set = new Set()));
    set.add(fn as (v: never) => void);
    return () => set.delete(fn as (v: never) => void);
  }

  private emit<K extends keyof EventMap>(kind: K, v: EventMap[K]) {
    this.listeners.get(kind)?.forEach((fn) => (fn as Listener<K>)(v));
  }

  start() {
    const url = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;
    const ws = new WebSocket(url);
    this.ws = ws;
    ws.onopen = () => {
      this.retry = 0;
      this.linkUp = true;
      this.emit('link', true);
    };
    ws.onmessage = (e) => this.handle(JSON.parse(e.data) as ServerMsg);
    ws.onclose = () => {
      if (this.ws !== ws) return;
      this.linkUp = false;
      this.emit('link', false);
      for (const [, p] of this.pending) {
        clearTimeout(p.timer);
        p.resolve({ ok: false, err: 'sin conexión con el gateway' });
      }
      this.pending.clear();
      const delay = Math.min(5000, 500 * 2 ** this.retry++);
      setTimeout(() => this.start(), delay);
    };
  }

  private handle(msg: ServerMsg) {
    switch (msg.type) {
      case 'snapshot':
        this.status = msg.status;
        this.schema = msg.schema;
        this.params = msg.params;
        this.emit('status', msg.status);
        this.emit('schema', msg.schema);
        this.emit('params', msg.params);
        break;
      case 'status':
        this.status = msg.status;
        this.emit('status', msg.status);
        break;
      case 'tel':
        this.emit('frames', msg.frames);
        break;
      case 'params':
        this.params = msg.params;
        this.emit('params', msg.params);
        break;
      case 'event':
        this.emit('event', msg.event);
        break;
      case 'ack': {
        const p = this.pending.get(msg.id);
        if (p) {
          clearTimeout(p.timer);
          this.pending.delete(msg.id);
          p.resolve(msg);
        }
        break;
      }
    }
  }

  /** Envía un comando al robot a través del gateway. Nunca rechaza: los errores vuelven en el Ack. */
  command(cmd: string, fields: Record<string, unknown> = {}): Promise<Ack> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      return Promise.resolve({ ok: false, err: 'sin conexión con el gateway' });
    }
    const id = this.nextId++;
    return new Promise((resolve) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(id);
        resolve({ ok: false, err: `sin respuesta a ${cmd}` });
      }, COMMAND_TIMEOUT_MS);
      this.pending.set(id, { resolve, timer });
      this.ws!.send(JSON.stringify({ type: 'cmd', id, cmd, ...fields }));
    });
  }
}

// ---------------------------------------------------------------- REST
export interface SerialPortInfo {
  device: string;
  description: string;
  usb: boolean;
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const r = await fetch(path, {
    method,
    headers: body ? { 'content-type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error((data as { detail?: string }).detail ?? `${r.status} ${r.statusText}`);
  return data as T;
}

export const api = {
  transports: () => request<{ serial: { ports: SerialPortInfo[]; baud: number } }>('GET', '/api/transports'),
  connect: (transport: 'serial' | 'demo', port?: string) => request<Status>('POST', '/api/connect', { transport, port }),
  disconnect: () => request<Status>('POST', '/api/disconnect'),
};
