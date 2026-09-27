/**
 * Punto de entrada de la HMI: conecta los módulos de la interfaz con el gateway.
 * Arquitectura y fases en docs/hmi/PLAN.md.
 */
import './styles/tokens.css';
import './styles/base.css';
import './styles/layout.css';

import { Gateway } from './core/gateway';
import { Traces } from './plots/traces';
import { PoseIntegrator } from './scene/pose';
import { RobotScene } from './scene/scene';
import { setupHistory } from './ui/history';
import { setupConnection } from './ui/connection';
import { setupCommands } from './ui/commands';
import { describeEvent, setupLog } from './ui/log';
import { setupParams } from './ui/params';
import { setupParamSets } from './ui/paramsets';
import { setupTheme } from './ui/theme';
import { setupTiles } from './ui/tiles';

const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;

const gw = new Gateway();

setupTheme($<HTMLButtonElement>('btnTema'));
setupConnection($('conexion'), $('conHint'), gw);
setupTiles($('tiles'), gw);
const log = setupLog($('log'), $<HTMLButtonElement>('btnLogLimpiar'), gw);
const note = (m: string, bad = false) => log.add('hmi', m, bad ? 'bad' : '');
setupParams($('parametros'), $('parHead'), gw, note);
setupParamSets($('juegos'), gw, note);
setupCommands($('comandos'), gw, note);

// ------------------------------------------------------------ en vivo: escena y trazas
const traces = new Traces($('plots'));
const integrator = new PoseIntegrator();
const scene = new RobotScene($('escenaVivo'), {
  onRecenter: () => {
    integrator.recenter();
    scene.setPose(integrator.pose);
  },
});
gw.on('frames', (fs) => {
  traces.push(fs);
  for (const f of fs) integrator.push(f);
  scene.setPose(integrator.pose);
});

let session: number | null = null;
gw.on('status', (s) => {
  if (s.session_id !== session) {
    session = s.session_id;
    if (session !== null) {
      traces.reset();
      integrator.reset();
      scene.setPose(null);
    }
  }
});

// ------------------------------------------------------------ pestañas: en vivo / historial
const history = setupHistory($('panelHistorial'), gw, (m, bad = false) => log.add('hmi', m, bad ? 'bad' : ''));
const tabBtns = [...document.querySelectorAll<HTMLButtonElement>('#cardVista .tabs button')];
function setTab(t: string) {
  tabBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.tab === t)));
  $('panelVivo').hidden = t !== 'vivo';
  $('panelHistorial').hidden = t !== 'historial';
  $('vivoCtl').hidden = t !== 'vivo';
  history.setActive(t === 'historial');
  try {
    localStorage.setItem('vista', t);
  } catch {
    /* sin almacenamiento */
  }
}
tabBtns.forEach((b) => b.addEventListener('click', () => setTab(b.dataset.tab!)));
setTab((() => {
  try {
    return localStorage.getItem('vista') === 'historial' ? 'historial' : 'vivo';
  } catch {
    return 'vivo';
  }
})());

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
  hint.style.visibility = p ? 'visible' : 'hidden'; // reserva su lugar: la barra no salta
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
  // Con un diálogo abierto, Esc sólo lo cierra
  if (e.key === 'Escape' && gw.status?.state === 'connected' && !document.querySelector('dialog[open]')) {
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
