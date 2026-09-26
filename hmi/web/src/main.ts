/**
 * Punto de entrada de la HMI: conecta los módulos de la interfaz con el gateway.
 * Arquitectura y fases en docs/hmi/PLAN.md.
 */
import './styles/tokens.css';
import './styles/base.css';
import './styles/layout.css';

import { Gateway } from './core/gateway';
import { Traces } from './plots/traces';
import { setupConnection } from './ui/connection';
import { describeEvent, setupLog } from './ui/log';
import { setupTheme } from './ui/theme';
import { setupTiles } from './ui/tiles';

const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;

const gw = new Gateway();

setupTheme($<HTMLButtonElement>('btnTema'));
setupConnection($('conexion'), $('conHint'), gw);
setupTiles($('tiles'), gw);
const log = setupLog($('log'), $<HTMLButtonElement>('btnLogLimpiar'), gw);

// ------------------------------------------------------------ trazas
const traces = new Traces($('plots'));
gw.on('frames', (fs) => traces.push(fs));

let session: number | null = null;
gw.on('status', (s) => {
  if (s.session_id !== session) {
    session = s.session_id;
    if (session !== null) traces.reset();
  }
});

// Marca en las trazas los eventos que cambian el comportamiento del robot
gw.on('event', (ev) => {
  if (ev.t_ms === null) return;
  if (ev.kind === 'cmd' && ev.payload?.ok && ['set', 'defaults', 'estop', 'arm', 'calib', 'deadband'].includes(ev.payload.cmd)) {
    traces.addMarker(ev.t_ms, describeEvent(ev).text);
  }
});

const seg = $('segVentana');
for (const s of [5, 10, 30, 60]) {
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = `${s} s`;
  b.setAttribute('aria-pressed', String(s === traces.windowS));
  b.addEventListener('click', () => {
    traces.setWindow(s);
    seg.querySelectorAll('button').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
  });
  seg.append(b);
}

const btnPausa = $<HTMLButtonElement>('btnPausa');
const hint = $('trazasHint');
function setPaused(p: boolean) {
  traces.setPaused(p);
  btnPausa.setAttribute('aria-pressed', String(p));
  btnPausa.textContent = p ? 'Reanudar' : 'Pausar';
  hint.hidden = !p;
}
btnPausa.addEventListener('click', () => setPaused(btnPausa.getAttribute('aria-pressed') !== 'true'));
setPaused(false);

// ------------------------------------------------------------ seguridad
const btnEstop = $<HTMLButtonElement>('btnEstop');
const btnArm = $<HTMLButtonElement>('btnArm');

async function send(cmd: string) {
  const ack = await gw.command(cmd);
  if (!ack.ok) log.add('hmi', `${cmd}: ${ack.err}`, 'bad');
}
btnEstop.addEventListener('click', () => send('estop'));
btnArm.addEventListener('click', () => send('arm'));
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && gw.status?.state === 'connected') {
    e.preventDefault();
    send('estop');
  }
});

function renderSafety() {
  const on = gw.linkUp && gw.status?.state === 'connected';
  btnEstop.disabled = btnArm.disabled = !on;
  btnArm.classList.toggle('primary', on && gw.status?.robot_state === 'ESTOP');
}
gw.on('status', renderSafety);
gw.on('link', renderSafety);

gw.start();
