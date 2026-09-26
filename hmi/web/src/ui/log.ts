/** Registro de eventos: mensajes del robot, comandos enviados y cambios de conexión. */
import type { Gateway } from '../core/gateway';
import type { GwEvent } from '../core/protocol';

const MAX_LINES = 300;

/** Texto corto de un evento; también lo usan las marcas de las trazas. */
export function describeEvent(ev: GwEvent): { kind: string; text: string; cls: string } {
  const p = ev.payload ?? {};
  switch (ev.kind) {
    case 'log':
      return { kind: 'robot', text: p.msg, cls: '' };
    case 'cmd': {
      const { cmd, ok, err, ...rest } = p;
      const args = cmd === 'set' && rest.params
        ? Object.entries(rest.params).map(([k, v]) => `${k}=${v}`).join(' ')
        : '';
      return { kind: 'cmd', text: `${cmd}${args ? ' ' + args : ''}${ok ? '' : ` ✗ ${err ?? ''}`}`, cls: ok ? 'cmd' : 'bad' };
    }
    case 'connect':
      return { kind: 'enlace', text: `conectado a ${p.target} (fw ${p.hello?.fw})`, cls: '' };
    case 'disconnect':
      return { kind: 'enlace', text: 'desconectado', cls: '' };
    case 'reboot':
      return { kind: 'enlace', text: 'el robot se reinició: nueva sesión', cls: 'bad' };
    case 'connection_lost':
      return { kind: 'enlace', text: p.reason ?? 'conexión perdida', cls: 'bad' };
    default:
      return { kind: ev.kind, text: JSON.stringify(p), cls: '' };
  }
}

const esc = (s: string) => s.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' })[c]!);

export function setupLog(root: HTMLElement, clearBtn: HTMLButtonElement, gw: Gateway) {
  function add(kind: string, text: string, cls = '', ts = Date.now() / 1000) {
    const atBottom = root.scrollTop + root.clientHeight >= root.scrollHeight - 4;
    root.querySelector('.vacio')?.remove();
    const time = new Date(ts * 1000).toLocaleTimeString('es-CO', { hour12: false });
    const div = document.createElement('div');
    div.className = `ln ${cls}`;
    div.innerHTML = `<span class="ts">${time}</span><span class="k">${esc(kind)}</span><span class="m">${esc(text)}</span>`;
    root.append(div);
    while (root.children.length > MAX_LINES) root.firstElementChild!.remove();
    if (atBottom) root.scrollTop = root.scrollHeight;
  }

  gw.on('event', (ev) => {
    const d = describeEvent(ev);
    add(d.kind, d.text, d.cls, ev.host_ts);
  });
  gw.on('link', (up) => add('hmi', up ? 'gateway conectado' : 'gateway desconectado: reintentando…', up ? '' : 'bad'));
  clearBtn.addEventListener('click', () => (root.innerHTML = '<div class="vacio">Sin eventos.</div>'));

  return { add };
}
