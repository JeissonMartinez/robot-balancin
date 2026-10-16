/**
 * Pestaña Motores (fase M0): banco de motores para diagnosticar los encoders y medir cada
 * motor en lazo abierto, con el robot sobre un soporte.
 *
 * - Banco: entrar (pausa el control) y salir (queda la parada activa: hay que Armar).
 * - PWM manual por motor, sin compensar zona muerta. Mientras sea ≠ 0 se renueva cada
 *   10 s; si la HMI se cierra, el robot lo apaga a los 30 s.
 * - Ensayo: tramos [PWM, ms] (escalón, escalera, ida y vuelta o a mano) sobre L, R o ambos,
 *   muestreados en el robot cada 1–20 ms. El gateway lo descarga y lo guarda; aquí se
 *   grafica con la RPM calculada de las cuentas y una tabla de RPM media y ruido por tramo.
 * - Encoders: cuentas, vueltas, RPM, flancos por canal, nivel de A y B, diagnóstico
 *   automático y prueba de vueltas a mano (cuentas por vuelta medidas).
 * - Lazo de RPM (M2, fw ≥ 0.7.0): referencia de la rueda derecha en vivo y configuración del
 *   lazo (Kp, Ki, período, ventana, prealimentación, izquierdo seguidor). Los ensayos pueden ser
 *   de PWM o de RPM; en los de RPM la tabla da subida, sobrepico, establecimiento y error.
 * - La rueda izquierda se muestra siempre, igual que la derecha.
 */
import type { Gateway } from '../core/gateway';
import { api } from '../core/gateway';
import type { Ack, BenchStatus, Capture, EncInfo, MotFrame, RpmConfig } from '../core/protocol';
import { Traces, type PlotDef } from '../plots/traces';
import { confirmDialog, escapeHtml } from './dialog';

type Log = (m: string, bad?: boolean) => void;
type Motor = 'L' | 'R' | 'LR';
type Preset = 'escalon' | 'escalera' | 'idavuelta' | 'manual';
type Kind = 'pwm' | 'rpm';

const DEFAULT_ENC: EncInfo = { ppr: 11, decode: 4, gear: 59.5, cpw: 2618 };
const KEEPALIVE_MS = 10_000;
const SEND_THROTTLE_MS = 120;
const MAX_SEGMENTS = 16;
const MAX_RPM = 60;
/** Valores iniciales del lazo (M1: K = 0.25 RPM/PWM, τ = 20 ms, λ = 50 ms), iguales a config.h. */
const DEFAULT_RPMCFG: RpmConfig = { kp: 1.33, ki: 67, ts_ms: 5, win_ms: 10, ff: true, ff_k: 0.25, ff_b: 1.8, i_max: 100, follow: true };

const LIVE_PLOTS: PlotDef[] = [
  {
    id: 'rpm', title: 'Velocidad de rueda', unit: 'RPM',
    series: [
      { key: 'rpmR', label: 'derecha', slot: 1, decimals: 1 },
      { key: 'rpmL', label: 'izquierda', slot: 2, decimals: 1 },
      { key: 'ref', label: 'referencia', slot: 3, dash: [6, 4], decimals: 1 },
    ],
  },
  {
    id: 'lazo', title: 'Mando del lazo de RPM', unit: 'PWM',
    series: [
      { key: 'uF', label: 'prealim.', slot: 3, decimals: 1 },
      { key: 'uP', label: 'P', slot: 4, decimals: 1 },
      { key: 'uI', label: 'I', slot: 5, decimals: 1 },
    ],
  },
  {
    id: 'pwm', title: 'PWM aplicado', unit: '',
    series: [
      { key: 'pwmR', label: 'derecha', slot: 1, decimals: 0 },
      { key: 'pwmL', label: 'izquierda', slot: 2, dash: [6, 4], decimals: 0 },
    ],
  },
  {
    id: 'cnt', title: 'Cuentas', unit: 'desde cero',
    series: [
      { key: 'zR', label: 'derecha', slot: 1, decimals: 0 },
      { key: 'zL', label: 'izquierda', slot: 2, decimals: 0 },
    ],
  },
];

const CAPTURE_PLOTS: PlotDef[] = [
  {
    id: 'rpm', title: 'Velocidad de rueda (de las cuentas)', unit: 'RPM',
    series: [
      { key: 'rpmR', label: 'derecha', slot: 1, decimals: 1 },
      { key: 'rpmL', label: 'izquierda', slot: 2, decimals: 1 },
      { key: 'ref', label: 'referencia', slot: 3, dash: [6, 4], decimals: 1 },
    ],
  },
  {
    id: 'pwm', title: 'PWM', unit: '',
    series: [
      { key: 'pwmR', label: 'derecha', slot: 1, decimals: 0 },
      { key: 'pwmL', label: 'izquierda', slot: 2, dash: [6, 4], decimals: 0 },
    ],
  },
  {
    id: 'cnt', title: 'Cuentas', unit: '',
    series: [
      { key: 'cR', label: 'derecha', slot: 1, decimals: 0 },
      { key: 'cL', label: 'izquierda', slot: 2, decimals: 0 },
    ],
  },
];

interface Form {
  kind: Kind;
  motor: Motor;
  preset: Preset;
  period: number;
  base: number;
  step: number;
  pre: number;
  dur: number;
  post: number;
  p0: number;
  dp: number;
  n: number;
  amp: number;
  pause: number;
  custom: string;
}

const DEFAULT_FORM: Form = {
  kind: 'pwm', motor: 'R', preset: 'escalon', period: 2, base: 0, step: 120, pre: 300, dur: 1500, post: 700,
  p0: 30, dp: 30, n: 7, amp: 120, pause: 500, custom: '0 300\n120 1500\n0 700',
};

function loadForm(): Form {
  try {
    return { ...DEFAULT_FORM, ...JSON.parse(localStorage.getItem('mot.form') ?? '{}') };
  } catch {
    return { ...DEFAULT_FORM };
  }
}

function saveForm(f: Form) {
  try {
    localStorage.setItem('mot.form', JSON.stringify(f));
  } catch {
    /* sin almacenamiento */
  }
}

/** Tramos del ensayo según el tipo elegido. Tramos de 0 ms se omiten. */
export function buildSequence(f: Form): { seq: [number, number][]; err?: string } {
  let seq: [number, number][] = [];
  switch (f.preset) {
    case 'escalon':
      seq = [[f.base, f.pre], [f.step, f.dur], [f.base, f.post]];
      break;
    case 'escalera':
      seq = [[0, f.pre]];
      for (let i = 0; i < f.n; i++) seq.push([f.p0 + i * f.dp, f.dur]);
      seq.push([0, f.post]);
      break;
    case 'idavuelta':
      seq = [[0, f.pre], [f.amp, f.dur], [0, f.pause], [-f.amp, f.dur], [0, f.post]];
      break;
    case 'manual':
      for (const line of f.custom.split('\n')) {
        const t = line.trim();
        if (!t || t.startsWith('#')) continue;
        const m = t.split(/[\s,;]+/).map(Number);
        const valOk = f.kind === 'rpm' ? Number.isFinite(m[0]) : Number.isInteger(m[0]);
        if (m.length !== 2 || !valOk || !Number.isInteger(m[1]))
          return { seq: [], err: `línea no válida: «${t}» (${f.kind === 'rpm' ? 'RPM y ms entero' : 'PWM y ms enteros'})` };
        seq.push([m[0], m[1]]);
      }
      break;
  }
  seq = seq.filter(([, ms]) => ms > 0);
  if (!seq.length) return { seq, err: 'el ensayo no tiene tramos' };
  if (seq.length > MAX_SEGMENTS) return { seq, err: `máximo ${MAX_SEGMENTS} tramos (hay ${seq.length})` };
  for (const [v, ms] of seq) {
    if (f.kind === 'rpm') {
      if (!Number.isFinite(v) || Math.abs(v) > MAX_RPM) return { seq, err: `RPM ${v} fuera de [−${MAX_RPM}, ${MAX_RPM}]` };
    } else if (!Number.isInteger(v) || v < -255 || v > 255) return { seq, err: `PWM ${v} fuera de [-255, 255]` };
    if (!Number.isInteger(ms) || ms > 60000) return { seq, err: `duración ${ms} ms fuera de [1, 60000]` };
  }
  return { seq };
}

/** RPM por diferencia centrada de las cuentas: ventana de 2·h muestras. */
function rpmFromCounts(t: number[], c: number[], h: number, cpw: number): (number | null)[] {
  return c.map((_, k) => {
    const a = Math.max(0, k - h);
    const b = Math.min(c.length - 1, k + h);
    const dt = t[b] - t[a];
    return dt > 0 ? ((c[b] - c[a]) / cpw / dt) * 60 : null;
  });
}

const fmt = (v: number | null | undefined, d = 1) => (v == null || !isFinite(v) ? '—' : v.toFixed(d));

export function setupMotors(root: HTMLElement, gw: Gateway, log: Log) {
  root.innerHTML = `
  <div class="mot">
    <div class="mot-ctl">
      <section class="mot-sec">
        <div class="mot-sec-h"><h3>Banco</h3><span class="pill" id="motEstado">—</span></div>
        <div class="mot-nota muted" id="motBancoNota">Robot sobre un soporte, con las dos ruedas en el aire. El control de equilibrio queda en pausa mientras el banco esté activo.</div>
        <div class="row">
          <button class="btn primary" type="button" id="motEntrar">Entrar al banco</button>
          <button class="btn" type="button" id="motSalir">Salir del banco</button>
        </div>
      </section>

      <section class="mot-sec">
        <div class="mot-sec-h"><h3>PWM manual</h3><span class="hint muted" id="motManualHint"></span></div>
        <div class="mot-pwm" data-m="L">
          <span class="name">Izquierdo <span class="key mono">canal B</span></span>
          <input class="num mono" type="number" min="-255" max="255" step="1" value="0" aria-label="PWM izquierdo">
          <button class="btn small cero" type="button" title="PWM 0">0</button>
          <input type="range" min="-255" max="255" step="1" value="0" aria-label="PWM izquierdo">
        </div>
        <div class="mot-pwm" data-m="R">
          <span class="name">Derecho <span class="key mono">canal A</span></span>
          <input class="num mono" type="number" min="-255" max="255" step="1" value="0" aria-label="PWM derecho">
          <button class="btn small cero" type="button" title="PWM 0">0</button>
          <input type="range" min="-255" max="255" step="1" value="0" aria-label="PWM derecho">
        </div>
        <div class="row">
          <label class="switch"><input type="checkbox" id="motJuntos"> Mismo PWM en ambos</label>
          <button class="btn small" type="button" id="motCeros" style="margin-left:auto">Ambos a 0</button>
        </div>
        <div class="mot-nota muted">Valor crudo de −255 a 255, sin compensar la zona muerta. PWM 0 frena el motor (freno en corto del driver).</div>
      </section>

      <section class="mot-sec">
        <div class="mot-sec-h"><h3>Lazo de RPM</h3><span class="pill" id="rpmEstado">PWM manual</span></div>
        <div class="mot-pwm" id="rpmRow">
          <span class="name">Referencia derecha <span class="key mono">RPM</span></span>
          <input class="num mono" type="number" min="-60" max="60" step="0.5" value="0" aria-label="Referencia de RPM">
          <button class="btn small cero" type="button" title="Referencia 0">0</button>
          <input type="range" min="-60" max="60" step="0.5" value="0" aria-label="Referencia de RPM">
        </div>
        <div class="mot-nota muted" id="rpmMedida">Mueve la referencia para activar el lazo. El izquierdo recibe el mismo PWM si está como seguidor.</div>
        <details class="grupo" id="rpmCfgBox">
          <summary>Configuración del lazo</summary>
          <div class="grupo-body">
            <div class="mot-grid">
              <label class="field"><span>Kp [PWM/RPM]</span><input type="number" data-c="kp" step="0.1" min="0" max="20"></label>
              <label class="field"><span>Ki [PWM/(RPM·s)]</span><input type="number" data-c="ki" step="1" min="0" max="2000"></label>
              <label class="field"><span>Período [ms]</span><select data-c="ts_ms"><option>1</option><option>2</option><option>5</option><option>10</option><option>20</option></select></label>
              <label class="field"><span>Ventana [ms]</span><select data-c="win_ms"><option>5</option><option>10</option><option>20</option><option>40</option></select></label>
              <label class="field"><span>Prealim. K [RPM/PWM]</span><input type="number" data-c="ff_k" step="0.01" min="0.05" max="1"></label>
              <label class="field"><span>Prealim. B [RPM]</span><input type="number" data-c="ff_b" step="0.1" min="0" max="20"></label>
              <label class="field"><span>Tope de I [PWM]</span><input type="number" data-c="i_max" step="5" min="0" max="255"></label>
              <div class="field"><span>Opciones</span>
                <label class="switch"><input type="checkbox" data-c="ff"> Prealimentación</label>
                <label class="switch"><input type="checkbox" data-c="follow"> Izquierdo seguidor</label>
              </div>
            </div>
            <div class="mot-nota mono" id="rpmCfgInfo"></div>
            <div class="row">
              <button class="btn small primary" type="button" id="rpmAplicar">Aplicar</button>
              <button class="btn small" type="button" id="rpmIniciales">Valores de M1</button>
            </div>
          </div>
        </details>
      </section>

      <section class="mot-sec">
        <div class="mot-sec-h"><h3>Ensayo</h3></div>
        <div class="mot-grid">
          <label class="field"><span>Motor</span>
            <div class="seg" id="motMotor"><button type="button" data-v="L">Izq.</button><button type="button" data-v="R">Der.</button><button type="button" data-v="LR">Ambos</button></div>
          </label>
          <label class="field"><span>Muestreo</span>
            <select id="motPeriodo"><option value="1">1 ms</option><option value="2">2 ms</option><option value="5">5 ms</option><option value="10">10 ms</option><option value="20">20 ms</option></select>
          </label>
          <label class="field full"><span>Variable</span>
            <div class="seg" id="motKind"><button type="button" data-v="pwm">PWM (lazo abierto)</button><button type="button" data-v="rpm">RPM (lazo cerrado)</button></div>
          </label>
          <label class="field full"><span>Tipo</span>
            <select id="motTipo">
              <option value="escalon">Escalón</option>
              <option value="escalera">Escalera (subida por pasos)</option>
              <option value="idavuelta">Ida y vuelta (+A, −A)</option>
              <option value="manual">Tramos a mano</option>
            </select>
          </label>
        </div>
        <div class="mot-grid" data-p="escalon">
          <label class="field"><span><span class="vname">PWM</span> base</span><input type="number" data-f="base" data-val step="1"></label>
          <label class="field"><span><span class="vname">PWM</span> escalón</span><input type="number" data-f="step" data-val step="1"></label>
          <label class="field"><span>Antes [ms]</span><input type="number" data-f="pre" min="0" step="50"></label>
          <label class="field"><span>Escalón [ms]</span><input type="number" data-f="dur" min="1" step="50"></label>
          <label class="field"><span>Después [ms]</span><input type="number" data-f="post" min="0" step="50"></label>
        </div>
        <div class="mot-grid" data-p="escalera">
          <label class="field"><span><span class="vname">PWM</span> inicial</span><input type="number" data-f="p0" data-val step="1"></label>
          <label class="field"><span>Paso de <span class="vname">PWM</span></span><input type="number" data-f="dp" data-val step="1"></label>
          <label class="field"><span>Escalones</span><input type="number" data-f="n" min="1" max="14" step="1"></label>
          <label class="field"><span>Cada uno [ms]</span><input type="number" data-f="dur" min="1" step="50"></label>
          <label class="field"><span>Antes [ms]</span><input type="number" data-f="pre" min="0" step="50"></label>
          <label class="field"><span>Después [ms]</span><input type="number" data-f="post" min="0" step="50"></label>
        </div>
        <div class="mot-grid" data-p="idavuelta">
          <label class="field"><span>Amplitud A [<span class="vname">PWM</span>]</span><input type="number" data-f="amp" data-val step="1"></label>
          <label class="field"><span>Cada sentido [ms]</span><input type="number" data-f="dur" min="1" step="50"></label>
          <label class="field"><span>Pausa [ms]</span><input type="number" data-f="pause" min="0" step="50"></label>
          <label class="field"><span>Antes [ms]</span><input type="number" data-f="pre" min="0" step="50"></label>
          <label class="field"><span>Después [ms]</span><input type="number" data-f="post" min="0" step="50"></label>
        </div>
        <div class="mot-grid" data-p="manual">
          <label class="field full"><span>Tramos: «<span class="vname">PWM</span> ms» por línea</span><textarea class="mono" data-f="custom" rows="5" spellcheck="false"></textarea></label>
        </div>
        <div class="mot-resumen mono" id="motResumen"></div>
        <div class="row">
          <button class="btn primary" type="button" id="motEjecutar">Ejecutar ensayo</button>
          <button class="btn" type="button" id="motDetener">Detener</button>
        </div>
        <div class="mot-prog" id="motProg" hidden><progress max="1" value="0"></progress><span class="mono"></span></div>
      </section>

      <section class="mot-sec">
        <div class="mot-sec-h"><h3>Encoders</h3><button class="btn small" type="button" id="motCero" style="margin-left:auto">Poner a cero</button></div>
        <table class="mot-enc mono">
          <thead><tr><th></th><th>Izquierdo</th><th>Derecho</th></tr></thead>
          <tbody>
            <tr><th>Cuentas</th><td id="encCL">—</td><td id="encCR">—</td></tr>
            <tr><th>Vueltas de rueda</th><td id="encVL">—</td><td id="encVR">—</td></tr>
            <tr><th>RPM</th><td id="encRL">—</td><td id="encRR">—</td></tr>
            <tr><th>Flancos A</th><td id="encAL">—</td><td id="encAR">—</td></tr>
            <tr><th>Flancos B</th><td id="encBL">—</td><td id="encBR">—</td></tr>
            <tr><th>Pines A · B</th><td id="encPL">—</td><td id="encPR">—</td></tr>
          </tbody>
        </table>
        <div class="mot-diag" id="encDiagL"></div>
        <div class="mot-diag" id="encDiagR"></div>
        <div class="mot-vueltas">
          <label class="field"><span>Vueltas dadas a mano</span><input type="number" id="encN" min="1" step="1" value="10"></label>
          <div class="mot-nota mono" id="encCpw"></div>
        </div>
        <div class="mot-nota muted">Poner a cero, girar cada rueda a mano un número exacto de vueltas hacia adelante y comparar las cuentas por vuelta con las esperadas (<span id="encEsperadas">2618</span>).</div>
      </section>
    </div>

    <div class="mot-vista">
      <div class="mot-vista-h">
        <div class="seg" id="motVistaSeg"><button type="button" data-v="vivo">En vivo</button><button type="button" data-v="ensayos">Ensayos</button></div>
        <div class="row mot-ens-bar" id="motEnsBar" hidden>
          <select id="motLista" aria-label="Ensayo"></select>
          <label class="field-inline">RPM con ventana
            <select id="motVentana"><option value="1">±1</option><option value="2">±2</option><option value="5" selected>±5</option><option value="10">±10</option><option value="25">±25</option></select>
          </label>
          <a class="btn small" id="motCsv" href="#" download>CSV</a>
          <button class="btn small" type="button" id="motBorrar">Borrar</button>
        </div>
      </div>
      <div class="mot-panel" id="motPanelVivo"><div class="plots" id="motPlotsVivo"></div></div>
      <div class="mot-panel" id="motPanelEns" hidden>
        <div class="mot-ens-info">
          <input type="text" id="motNotas" placeholder="Notas del ensayo (p. ej. batería 8.2 V, rueda en el aire)">
          <span class="mot-nota mono" id="motEnsMeta"></span>
        </div>
        <div class="plots" id="motPlotsEns"></div>
        <div class="tabla-wrap mot-tramos"><table class="tel" id="motTramos"></table></div>
      </div>
    </div>
  </div>`;

  const $ = <T extends HTMLElement = HTMLElement>(id: string) => root.querySelector<T>('#' + id)!;
  const connected = () => gw.linkUp && gw.status?.state === 'connected';
  const supported = () => !!gw.status?.bench;
  const enc = () => gw.status?.enc ?? DEFAULT_ENC;

  // ------------------------------------------------------------ estado
  let bench: BenchStatus | null = null;
  let last: MotFrame | null = null;
  let zero: MotFrame | null = null;
  let frameAt = 0; // ms locales de la última trama "mot"
  const recent: MotFrame[] = []; // ~1 s para el diagnóstico de sentido y cables cruzados
  const desired = { L: 0, R: 0 };
  let form = loadForm();

  const benchOn = () => !!bench?.on || (connected() && gw.status?.robot_state === 'BENCH' && Date.now() - frameAt < 1500);
  const capRunning = () => (last?.cs ?? bench?.cap.st) === 'running' && benchOn();

  async function cmd(op: string, fields: Record<string, unknown> = {}): Promise<Ack> {
    const ack = await gw.command('bench', { op, ...fields });
    if (ack.ok && ack.bench) {
      bench = ack.bench as BenchStatus;
      fillCfg(bench.rpmcfg);
    }
    if (!ack.ok) log(`banco ${op}: ${ack.err}`, true);
    render();
    return ack;
  }

  // ------------------------------------------------------------ banco
  $('motEntrar').addEventListener('click', async () => {
    const ok = await confirmDialog({
      title: 'Entrar al banco de motores',
      body: '<p><b>Pon el robot sobre un soporte, con las dos ruedas en el aire.</b> Los motores van a girar con el PWM que pidas.</p><p>El control de equilibrio queda en pausa. Al salir del banco queda activa la parada de emergencia: hay que <b>Armar</b> para volver a equilibrar.</p>',
      ok: 'Entrar', danger: true,
    });
    if (!ok) return;
    const ack = await cmd('on');
    if (ack.ok) {
      zero = null; // la primera trama del banco fija el cero
      setPwmUi(0, 0);
    }
  });
  $('motSalir').addEventListener('click', async () => {
    const ack = await cmd('off');
    if (ack.ok) {
      setPwmUi(0, 0);
      log('Banco cerrado: parada de emergencia activa (Armar para volver al control).');
    }
  });

  // ------------------------------------------------------------ PWM manual
  const rows = { L: root.querySelector<HTMLElement>('.mot-pwm[data-m="L"]')!, R: root.querySelector<HTMLElement>('.mot-pwm[data-m="R"]')! };
  const chkLink = $<HTMLInputElement>('motJuntos');
  let sendTimer = 0;
  let lastSend = 0;

  function setPwmUi(l: number, r: number) {
    desired.L = l;
    desired.R = r;
    for (const m of ['L', 'R'] as const) {
      const v = String(desired[m]);
      rows[m].querySelector<HTMLInputElement>('.num')!.value = v;
      rows[m].querySelector<HTMLInputElement>('input[type=range]')!.value = v;
    }
  }

  async function sendPwm() {
    sendTimer = 0;
    lastSend = Date.now();
    const ack = await cmd('pwm', { l: desired.L, r: desired.R });
    if (!ack.ok && bench) setPwmUi(bench.pwm[0], bench.pwm[1]);
    if (ack.ok) setRefUi(0); // el mando pasa a PWM manual
  }

  function schedulePwm(now = false) {
    if (now) {
      clearTimeout(sendTimer);
      return sendPwm();
    }
    if (sendTimer) return;
    const wait = Math.max(0, SEND_THROTTLE_MS - (Date.now() - lastSend));
    sendTimer = window.setTimeout(sendPwm, wait);
  }

  function changePwm(m: 'L' | 'R', v: number, now: boolean) {
    v = Math.max(-255, Math.min(255, Math.round(v) || 0));
    if (chkLink.checked) setPwmUi(v, v);
    else setPwmUi(m === 'L' ? v : desired.L, m === 'R' ? v : desired.R);
    schedulePwm(now);
  }

  for (const m of ['L', 'R'] as const) {
    const num = rows[m].querySelector<HTMLInputElement>('.num')!;
    const range = rows[m].querySelector<HTMLInputElement>('input[type=range]')!;
    range.addEventListener('input', () => changePwm(m, Number(range.value), false));
    range.addEventListener('change', () => changePwm(m, Number(range.value), true));
    num.addEventListener('change', () => changePwm(m, Number(num.value), true));
    rows[m].querySelector('.cero')!.addEventListener('click', () => changePwm(m, 0, true));
  }
  $('motCeros').addEventListener('click', () => {
    setPwmUi(0, 0);
    schedulePwm(true);
  });
  // ------------------------------------------------------------ lazo de RPM
  const rpmRow = $('rpmRow');
  const rpmNum = rpmRow.querySelector<HTMLInputElement>('.num')!;
  const rpmRange = rpmRow.querySelector<HTMLInputElement>('input[type=range]')!;
  let desiredRef = 0;
  let rpmTimer = 0;
  let rpmLast = 0;

  function setRefUi(v: number) {
    desiredRef = v;
    rpmNum.value = String(v);
    rpmRange.value = String(v);
  }

  async function sendRpm() {
    rpmTimer = 0;
    rpmLast = Date.now();
    const ack = await cmd('rpm', { r: desiredRef });
    if (ack.ok) setPwmUi(0, 0); // el mando pasa al lazo
    else if (bench) setRefUi(bench.ref ?? 0);
  }

  function changeRef(v: number, now: boolean) {
    v = Math.max(-MAX_RPM, Math.min(MAX_RPM, Math.round((Number(v) || 0) * 2) / 2));
    setRefUi(v);
    if (now) {
      clearTimeout(rpmTimer);
      sendRpm();
    } else if (!rpmTimer) {
      rpmTimer = window.setTimeout(sendRpm, Math.max(0, SEND_THROTTLE_MS - (Date.now() - rpmLast)));
    }
  }

  rpmRange.addEventListener('input', () => changeRef(Number(rpmRange.value), false));
  rpmRange.addEventListener('change', () => changeRef(Number(rpmRange.value), true));
  rpmNum.addEventListener('change', () => changeRef(Number(rpmNum.value), true));
  rpmRow.querySelector('.cero')!.addEventListener('click', () => changeRef(0, true));

  // Configuración del lazo: se edita aquí y se envía con Aplicar
  const cfgEls = [...root.querySelectorAll<HTMLInputElement | HTMLSelectElement>('[data-c]')];
  let cfgDirty = false;

  function writeCfg(c: RpmConfig) {
    for (const el of cfgEls) {
      const v = c[el.dataset.c as keyof RpmConfig];
      if (el instanceof HTMLInputElement && el.type === 'checkbox') el.checked = !!v;
      else el.value = String(v);
    }
    cfgInfo();
  }

  function fillCfg(c: RpmConfig | undefined) {
    if (c && !cfgDirty) writeCfg(c);
  }

  function readCfg(): RpmConfig {
    const c = { ...DEFAULT_RPMCFG } as Record<string, unknown>;
    for (const el of cfgEls) {
      const k = el.dataset.c!;
      c[k] = el instanceof HTMLInputElement && el.type === 'checkbox' ? el.checked : Number(el.value);
    }
    return c as unknown as RpmConfig;
  }

  /** Lo que significan Kp y Ki con el modelo de M1 (τ = 20 ms, K = 0.25 RPM/PWM). */
  function cfgInfo() {
    const c = readCfg();
    const tau = 0.02;
    const k = 0.25;
    const lTot = (c.win_ms / 2 + c.ts_ms / 2 + 2) / 1000; // ventana + período + motor
    const ti = c.ki > 0 ? c.kp / c.ki : Infinity;
    const lambda = c.kp > 0 ? tau / (k * c.kp) - lTot : Infinity;
    const res = 60 / (enc().cpw * c.win_ms * 1e-3);
    $('rpmCfgInfo').textContent =
      `Ti = Kp/Ki = ${isFinite(ti) ? (ti * 1000).toFixed(0) + ' ms' : '∞'} (τ del motor: 20 ms) · ` +
      `λ ≈ ${isFinite(lambda) ? Math.max(0, lambda * 1000).toFixed(0) + ' ms' : '—'} · ` +
      `ventana: ${res.toFixed(2)} RPM por cuenta, ~${(c.win_ms / 2).toFixed(0)} ms de retardo`;
  }

  cfgEls.forEach((el) =>
    el.addEventListener(el instanceof HTMLInputElement && el.type === 'checkbox' ? 'change' : 'input', () => {
      cfgDirty = true;
      cfgInfo();
    }),
  );
  $('rpmAplicar').addEventListener('click', async () => {
    const ack = await cmd('rpmcfg', { ...readCfg() });
    if (ack.ok) {
      cfgDirty = false;
      fillCfg(bench?.rpmcfg);
      log('lazo de RPM: configuración aplicada');
    }
  });
  $('rpmIniciales').addEventListener('click', () => {
    cfgDirty = true;
    writeCfg(DEFAULT_RPMCFG);
  });
  writeCfg(DEFAULT_RPMCFG);

  // El robot apaga el mando manual si no se renueva en 30 s
  setInterval(() => {
    if (!benchOn() || capRunning()) return;
    if (last?.mode === 'rpm' && desiredRef) sendRpm();
    else if (desired.L || desired.R) sendPwm();
  }, KEEPALIVE_MS);

  // ------------------------------------------------------------ ensayo
  const segMotor = $('motMotor');
  const segKind = $('motKind');
  const selTipo = $<HTMLSelectElement>('motTipo');
  const selPeriodo = $<HTMLSelectElement>('motPeriodo');
  const fieldInputs = [...root.querySelectorAll<HTMLInputElement | HTMLTextAreaElement>('[data-f]')];

  function fillForm() {
    segMotor.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.v === form.motor)));
    segKind.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.v === form.kind)));
    root.querySelectorAll('.vname').forEach((el) => (el.textContent = form.kind === 'rpm' ? 'RPM' : 'PWM'));
    root.querySelectorAll<HTMLInputElement>('[data-val]').forEach((el) => (el.step = form.kind === 'rpm' ? '0.5' : '1'));
    selTipo.value = form.preset;
    selPeriodo.value = String(form.period);
    for (const el of fieldInputs) {
      const k = el.dataset.f as keyof Form;
      el.value = String(form[k]);
    }
    root.querySelectorAll<HTMLElement>('.mot-grid[data-p]').forEach((g) => (g.hidden = g.dataset.p !== form.preset));
    summarize();
  }

  function readForm() {
    form.preset = selTipo.value as Preset;
    form.period = Number(selPeriodo.value);
    // Un campo puede estar en varios tipos (p. ej. «dur»): manda el del tipo visible
    for (const el of fieldInputs) {
      const grid = el.closest<HTMLElement>('.mot-grid[data-p]');
      if (grid && grid.dataset.p !== form.preset) continue;
      const k = el.dataset.f as keyof Form;
      const v = Number(el.value) || 0;
      (form as unknown as Record<string, unknown>)[k] =
        k === 'custom' ? el.value : el.hasAttribute('data-val') && form.kind === 'rpm' ? Math.round(v * 10) / 10 : Math.round(v);
    }
    saveForm(form);
  }

  function summarize() {
    const { seq, err } = buildSequence(form);
    const out = $('motResumen');
    const btn = $<HTMLButtonElement>('motEjecutar');
    if (err) {
      out.innerHTML = `<span class="err">${escapeHtml(err)}</span>`;
      btn.dataset.invalid = '1';
    } else {
      const ms = seq.reduce((a, [, d]) => a + d, 0);
      const n = Math.floor(ms / form.period) + 1;
      const max = bench?.max_samples ?? 5000;
      const tramos = seq.map(([p, d]) => `${p}·${d}`).join('  ');
      const lazo = form.kind === 'rpm' ? 'RPM, lazo de la rueda derecha · ' : '';
      out.innerHTML = `${lazo}${seq.length} tramos · ${(ms / 1000).toFixed(2)} s · <span class="${n > max ? 'err' : ''}">${n} muestras${n > max ? ` (máx. ${max})` : ''}</span><br><span class="muted">${tramos}</span>`;
      if (n > max) btn.dataset.invalid = '1';
      else delete btn.dataset.invalid;
    }
    render();
  }

  segMotor.querySelectorAll<HTMLButtonElement>('button').forEach((b) =>
    b.addEventListener('click', () => {
      form.motor = b.dataset.v as Motor;
      saveForm(form);
      fillForm();
    }),
  );
  segKind.querySelectorAll<HTMLButtonElement>('button').forEach((b) =>
    b.addEventListener('click', () => {
      const kind = b.dataset.v as Kind;
      if (kind === form.kind) return;
      // Convierte los valores con la recta de M0 (RPM ≈ 0.25 · PWM) para que sigan teniendo sentido
      const conv = (v: number) => (kind === 'rpm' ? Math.round(v * 0.25 * 2) / 2 : Math.round(v / 0.25));
      for (const k of ['base', 'step', 'p0', 'dp', 'amp'] as const) form[k] = conv(form[k]);
      form.kind = kind;
      saveForm(form);
      fillForm();
    }),
  );
  selTipo.addEventListener('change', () => {
    form.preset = selTipo.value as Preset;
    saveForm(form);
    fillForm();
  });
  selPeriodo.addEventListener('change', () => {
    readForm();
    summarize();
  });
  fieldInputs.forEach((el) => el.addEventListener('input', () => {
    readForm();
    summarize();
  }));

  $('motEjecutar').addEventListener('click', async () => {
    readForm();
    const { seq, err } = buildSequence(form);
    if (err) return log(`ensayo: ${err}`, true);
    setPwmUi(0, 0);
    setRefUi(0);
    const ack = await cmd('run', { motor: form.motor, kind: form.kind, seq, period_ms: form.period });
    if (ack.ok) {
      setView('ensayos');
      waitingCapture = bench?.cap.id ?? null;
    }
  });
  $('motDetener').addEventListener('click', () => {
    setPwmUi(0, 0);
    cmd('stop');
  });

  // ------------------------------------------------------------ encoders
  $('motCero').addEventListener('click', () => (zero = last));
  $<HTMLInputElement>('encN').addEventListener('input', renderEncoders);

  function pins(lv: number, shift: number) {
    const a = (lv >> shift) & 1;
    const b = (lv >> (shift + 1)) & 1;
    return `A ${a ? '●' : '○'} · B ${b ? '●' : '○'}`;
  }

  /** Diagnóstico de un encoder desde el último cero. */
  function diagnose(side: 'L' | 'R'): { text: string; cls: string } {
    if (!last || !zero) return { text: '', cls: '' };
    const name = side === 'L' ? 'Izquierdo' : 'Derecho';
    const pinsTxt = side === 'L' ? 'GPIO 9 (A) y 11 (B)' : 'GPIO 3 (A) y 46 (B)';
    const eA = last[`e${side}A` as const] - zero[`e${side}A` as const];
    const eB = last[`e${side}B` as const] - zero[`e${side}B` as const];
    const c = Math.abs(last[`c${side}` as const] - zero[`c${side}` as const]);
    const other = side === 'L' ? 'R' : 'L';

    // Sentido y cables cruzados, con el PWM manual aplicado a un solo motor
    const drive = recent.filter((f) => Math.abs(f[`pwm${side}` as const]) >= 40 && Math.abs(f[`pwm${other}` as const]) === 0);
    if (drive.length >= 10) {
      const rpm = drive.reduce((a, f) => a + f[`rpm${side}` as const], 0) / drive.length;
      const rpmOther = drive.reduce((a, f) => a + f[`rpm${other}` as const], 0) / drive.length;
      const pwm = drive[drive.length - 1][`pwm${side}` as const];
      if (Math.abs(rpm) < 1 && Math.abs(rpmOther) > 3)
        return {
          text: `${name}: con PWM sólo en este motor cuenta el otro encoder. Mirar qué rueda gira: si gira la otra, están cruzados los canales del driver (CH_A/CH_B en config.h); si gira ésta, los cables de encoder.`,
          cls: 'mal',
        };
      if (Math.abs(rpm) > 3 && Math.sign(rpm) !== Math.sign(pwm))
        return { text: `${name}: con PWM ${pwm > 0 ? 'positivo' : 'negativo'} la RPM sale ${rpm > 0 ? 'positiva' : 'negativa'}: invertir ENC_${side}_SIGN en config.h.`, cls: 'warn' };
    }
    if (eA + eB < 8) return { text: `${name}: sin movimiento desde el cero. Girar la rueda o aplicar PWM.`, cls: '' };
    if (side === 'L' && enc().single_l) {
      return {
        text: `${name}: modo de un canal (ENC_L_SINGLE_CHANNEL). Cuenta los flancos de A (${eA}) con el sentido del último PWM; B no se usa (${eB} flancos). Al girar a mano el sentido es el del último PWM aplicado.`,
        cls: eA ? 'info' : 'warn',
      };
    }
    const lo = Math.min(eA, eB);
    const hi = Math.max(eA, eB);
    if (lo === 0 || lo / hi < 0.1) {
      const dead = eA < eB ? 'A' : 'B';
      return { text: `${name}: el canal ${dead} no tiene pulsos (${eA} / ${eB}). Sin una fase el contador no puede contar: revisar sensor, cable y conector (${pinsTxt}).`, cls: 'mal' };
    }
    const q = c / (eA + eB);
    if (lo / hi < 0.8)
      return { text: `${name}: canales desbalanceados (A ${eA}, B ${eB}): contacto intermitente o ruido.`, cls: 'warn' };
    return {
      text: `${name}: ambos canales con pulsos. Cuentas / flancos = ${(q * 100).toFixed(0)} % (≈ 100 % si giró en un solo sentido; menos indica ruido filtrado o cambios de sentido).`,
      cls: q > 0.9 ? 'ok' : 'info',
    };
  }

  function renderEncoders() {
    const e = enc();
    $('encEsperadas').textContent = String(e.cpw);
    if (!last) return;
    const z = zero ?? last;
    for (const s of ['L', 'R'] as const) {
      const c = last[`c${s}` as const] - z[`c${s}` as const];
      $(`encC${s}`).textContent = String(c);
      $(`encV${s}`).textContent = fmt(c / e.cpw, 3);
      $(`encR${s}`).textContent = fmt(last[`rpm${s}` as const], 1);
      $(`encA${s}`).textContent = String(last[`e${s}A` as const] - z[`e${s}A` as const]);
      $(`encB${s}`).textContent = String(last[`e${s}B` as const] - z[`e${s}B` as const]);
      $(`encP${s}`).textContent = pins(last.lv, s === 'L' ? 0 : 2);
      const d = diagnose(s);
      const el = $(`encDiag${s}`);
      el.textContent = d.text;
      el.className = `mot-diag ${d.cls}`;
    }
    const n = Number($<HTMLInputElement>('encN').value) || 0;
    if (n > 0) {
      const per = (s: 'L' | 'R') => Math.abs(last![`c${s}` as const] - z[`c${s}` as const]) / n;
      const pct = (v: number) => `${((v / e.cpw) * 100).toFixed(1)} %`;
      $('encCpw').innerHTML = `Cuentas por vuelta: izq. <b>${per('L').toFixed(1)}</b> (${pct(per('L'))}) · der. <b>${per('R').toFixed(1)}</b> (${pct(per('R'))})`;
    }
  }

  // ------------------------------------------------------------ trazas en vivo
  const liveTraces = new Traces($('motPlotsVivo'), { plots: LIVE_PLOTS, syncKey: 'motores' });
  gw.on('mot', (fs) => {
    if (!fs.length) return;
    if (!zero) zero = fs[0];
    last = fs[fs.length - 1];
    frameAt = Date.now();
    recent.push(...fs);
    if (recent.length > 50) recent.splice(0, recent.length - 50);
    const z = zero;
    liveTraces.push(
      fs.map((f) => {
        const rpm = f.mode === 'rpm';
        return {
          ...f, zL: f.cL - z.cL, zR: f.cR - z.cR,
          ref: rpm ? f.ref : null, uF: rpm ? f.uF : null, uP: rpm ? f.uP : null, uI: rpm ? f.uI : null,
        };
      }),
    );
  });
  gw.on('status', (s) => {
    if (s.state !== 'connected') {
      bench = null;
      last = null;
    }
  });

  // ------------------------------------------------------------ ensayos guardados
  const capTraces = new Traces($('motPlotsEns'), { mode: 'replay', plots: CAPTURE_PLOTS, syncKey: 'ensayo' });
  const selLista = $<HTMLSelectElement>('motLista');
  const selVentana = $<HTMLSelectElement>('motVentana');
  let captures: Capture[] = [];
  let current: Capture | null = null;
  let waitingCapture: number | null = null;

  async function loadList(selectId?: number) {
    try {
      captures = await api.captures();
    } catch {
      return;
    }
    const keep = selectId ?? current?.id;
    selLista.innerHTML = captures.length
      ? captures
          .map((c) => {
            const when = new Date(c.created_at).toLocaleString('es-CO', { hour12: false, month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit' });
            const tag = c.state === 'aborted' ? ' · abortado' : '';
            return `<option value="${c.id}">#${c.id} · motor ${c.motor} · ${c.n} muestras · ${when}${tag}${c.notes ? ' · ' + escapeHtml(c.notes) : ''}</option>`;
          })
          .join('')
      : '<option value="">Sin ensayos guardados</option>';
    const target = captures.find((c) => c.id === keep) ?? captures[0];
    if (target) {
      selLista.value = String(target.id);
      if (target.id !== current?.id || selectId !== undefined) await open(target.id);
    } else {
      current = null;
      capTraces.load([], {});
      renderCapture();
    }
  }

  async function open(id: number) {
    try {
      current = await api.capture(id);
    } catch (e) {
      log(`no se pudo abrir el ensayo ${id}: ${(e as Error).message}`, true);
      return;
    }
    renderCapture();
  }

  function renderCapture() {
    const c = current;
    const csv = $<HTMLAnchorElement>('motCsv');
    $<HTMLButtonElement>('motBorrar').disabled = !c;
    csv.toggleAttribute('hidden', !c);
    if (!c?.rows) {
      $('motEnsMeta').textContent = '';
      $('motTramos').innerHTML = '';
      return;
    }
    csv.href = api.captureCsvUrl(c.id);
    $<HTMLInputElement>('motNotas').value = c.notes ?? '';
    const cpw = c.enc?.cpw ?? DEFAULT_ENC.cpw;
    const h = Number(selVentana.value);
    const t = c.rows.map((r) => r[0] * 1e-6);
    const cL = c.rows.map((r) => r[3]);
    const cR = c.rows.map((r) => r[4]);
    const rpmL = rpmFromCounts(t, cL, h, cpw);
    const rpmR = rpmFromCounts(t, cR, h, cpw);
    const isRpm = c.kind === 'rpm' && c.rows[0]?.length > 5;
    const ref = c.rows.map((r) => (isRpm ? r[5] / 10 : null));
    capTraces.load(t, {
      rpmL, rpmR, cL, cR, ref,
      pwmL: c.rows.map((r) => r[1]),
      pwmR: c.rows.map((r) => r[2]),
    });
    const res = (60 / (cpw * 2 * h * c.period_ms * 1e-3)).toFixed(2);
    const lazo = isRpm && c.cfg
      ? `lazo Kp ${c.cfg.kp} · Ki ${c.cfg.ki} · período ${c.cfg.ts_ms} ms · ventana ${c.cfg.win_ms} ms · prealim. ${c.cfg.ff ? 'sí' : 'no'} · `
      : '';
    $('motEnsMeta').textContent =
      `${isRpm ? 'RPM · ' + lazo : ''}motor ${c.motor} · muestreo ${c.period_ms} ms · ventana ${2 * h * c.period_ms} ms → resolución ${res} RPM por cuenta · ${cpw} cuentas/vuelta`;
    if (isRpm) renderStepMetrics(c, t, cR, rpmR, cpw);
    else renderSegments(c, t, cL, cR, rpmL, rpmR, cpw);
  }

  /**
   * Ensayo de RPM: por tramo, RPM media en régimen (segunda mitad, de las cuentas), error y, si
   * la referencia cambió, subida 10–90 %, sobrepico y establecimiento (banda ±5 % del cambio,
   * mínimo ±1.5 RPM por el ruido de medición).
   */
  function renderStepMetrics(c: Capture, t: number[], cR: number[], rpm: (number | null)[], cpw: number) {
    let start = 0;
    let prev = 0;
    const lines = c.seq.map(([r1, ms], i) => {
      const t0 = start;
      const end = start + ms / 1000;
      start = end;
      const r0 = prev;
      prev = r1;
      const idx = t.map((v, k) => (v >= t0 && v < end ? k : -1)).filter((k) => k >= 0);
      const half = idx.filter((k) => t[k] >= t0 + (end - t0) / 2);
      let mean: number | null = null;
      if (half.length > 1) {
        const a = half[0];
        const b = half[half.length - 1];
        mean = ((cR[b] - cR[a]) / cpw / (t[b] - t[a])) * 60;
      }
      const d = r1 - r0;
      let rise = '—';
      let over = '—';
      let settle = '—';
      if (Math.abs(d) >= 0.5 && idx.length > 2) {
        const frac = (k: number) => ((rpm[k] ?? r0) - r0) / d;
        const k10 = idx.find((k) => frac(k) >= 0.1);
        const k90 = idx.find((k) => frac(k) >= 0.9);
        if (k10 !== undefined && k90 !== undefined) rise = ((t[k90] - t[k10]) * 1000).toFixed(0);
        const peak = Math.max(...idx.map(frac));
        over = Math.max(0, (peak - 1) * 100).toFixed(0);
        const band = Math.max(0.05 * Math.abs(d), 1.5);
        const out = idx.filter((k) => Math.abs((rpm[k] ?? r0) - r1) > band);
        const lastOut = out.length ? out[out.length - 1] : idx[0];
        settle = lastOut === idx[idx.length - 1] ? 'no' : ((t[lastOut] - t0) * 1000 + c.period_ms).toFixed(0);
      }
      const err = mean == null ? null : r1 - mean;
      return `<tr><td>${i + 1}</td><td>${r1}</td><td>${ms}</td><td>${fmt(mean, 2)}</td><td>${fmt(err, 2)}</td><td>${rise}</td><td>${over}</td><td>${settle}</td></tr>`;
    });
    $('motTramos').innerHTML = `
      <thead><tr><th>Tramo</th><th>Ref [RPM]</th><th>ms</th><th>RPM der.</th><th>Error</th><th>Subida 10–90 [ms]</th><th>Sobrepico [%]</th><th>Estab. [ms]</th></tr></thead>
      <tbody>${lines.join('')}</tbody>
      <tfoot><tr><td colspan="8" class="muted" style="text-align:left">RPM media y error en la segunda mitad del tramo. Subida, sobrepico y establecimiento (±5 % del cambio, mínimo ±1.5 RPM) con la RPM de la ventana elegida.</td></tr></tfoot>`;
  }

  /** Por tramo: RPM media (cuentas en la segunda mitad / tiempo) y desviación de la RPM con ventana. */
  function renderSegments(c: Capture, t: number[], cL: number[], cR: number[], rpmL: (number | null)[], rpmR: (number | null)[], cpw: number) {
    let start = 0;
    const lines = c.seq.map(([pwm, ms], i) => {
      const end = start + ms / 1000;
      const from = start + (end - start) / 2;
      start = end;
      const idx = t.map((v, k) => (v >= from && v < end ? k : -1)).filter((k) => k >= 0);
      const cell = (counts: number[], rpm: (number | null)[]) => {
        if (idx.length < 2) return '<td>—</td><td>—</td>';
        const a = idx[0];
        const b = idx[idx.length - 1];
        const mean = ((counts[b] - counts[a]) / cpw / (t[b] - t[a])) * 60;
        const vals = idx.map((k) => rpm[k]).filter((v): v is number => v != null);
        const m = vals.reduce((x, y) => x + y, 0) / vals.length;
        const sd = Math.sqrt(vals.reduce((x, y) => x + (y - m) ** 2, 0) / Math.max(1, vals.length - 1));
        return `<td>${fmt(mean, 2)}</td><td>${fmt(sd, 2)}</td>`;
      };
      return `<tr><td>${i + 1}</td><td>${pwm}</td><td>${ms}</td>${cell(cL, rpmL)}${cell(cR, rpmR)}</tr>`;
    });
    $('motTramos').innerHTML = `
      <thead><tr><th>Tramo</th><th>PWM</th><th>ms</th><th>RPM izq.</th><th>σ izq.</th><th>RPM der.</th><th>σ der.</th></tr></thead>
      <tbody>${lines.join('')}</tbody>
      <tfoot><tr><td colspan="7" class="muted" style="text-align:left">Media y σ en la segunda mitad de cada tramo (régimen). σ depende de la ventana elegida.</td></tr></tfoot>`;
  }

  selLista.addEventListener('change', () => selLista.value && open(Number(selLista.value)));
  selVentana.addEventListener('change', renderCapture);
  $<HTMLInputElement>('motNotas').addEventListener('change', async () => {
    if (!current) return;
    try {
      await api.patchCapture(current.id, $<HTMLInputElement>('motNotas').value);
    } catch (e) {
      log(`notas: ${(e as Error).message}`, true);
    }
  });
  $('motBorrar').addEventListener('click', async () => {
    if (!current) return;
    const ok = await confirmDialog({ title: `Borrar el ensayo #${current.id}`, body: '<p>Se borra de la base de datos de este PC.</p>', ok: 'Borrar', danger: true });
    if (!ok) return;
    try {
      await api.deleteCapture(current.id);
      current = null;
    } catch (e) {
      log(`borrar: ${(e as Error).message}`, true);
    }
  });
  gw.on('capturesChanged', (id) => {
    // Un ensayo recién guardado se abre solo si es el que se acaba de pedir desde aquí
    const isNew = !captures.some((c) => c.id === id);
    loadList(isNew && waitingCapture !== null ? id : undefined);
    if (isNew) waitingCapture = null;
  });
  gw.on('link', (up) => up && loadList());

  // ------------------------------------------------------------ vista
  const segVista = $('motVistaSeg');
  function setView(v: 'vivo' | 'ensayos') {
    segVista.querySelectorAll('button').forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.v === v)));
    $('motPanelVivo').hidden = v !== 'vivo';
    $('motPanelEns').hidden = v !== 'ensayos';
    $('motEnsBar').hidden = v !== 'ensayos';
    try {
      localStorage.setItem('mot.vista', v);
    } catch {
      /* sin almacenamiento */
    }
  }
  segVista.querySelectorAll<HTMLButtonElement>('button').forEach((b) => b.addEventListener('click', () => setView(b.dataset.v as 'vivo' | 'ensayos')));

  // ------------------------------------------------------------ estado de los controles
  function render() {
    const on = connected() && supported();
    const active = on && benchOn();
    const running = active && capRunning();
    const estop = last ? !!last.es : !!bench?.estop;
    const pill = $('motEstado');
    if (!connected()) {
      pill.textContent = 'sin robot';
      pill.className = 'pill';
    } else if (!supported()) {
      pill.textContent = 'fw sin banco';
      pill.className = 'pill mal';
    } else if (running) {
      pill.textContent = 'ensayo en curso';
      pill.className = 'pill warn';
    } else if (active) {
      pill.textContent = estop ? 'activo · parada' : 'activo';
      pill.className = estop ? 'pill mal' : 'pill ok';
    } else {
      pill.textContent = 'inactivo';
      pill.className = 'pill';
    }
    $('motBancoNota').textContent = connected() && !supported()
      ? `El firmware del robot (${gw.status?.fw ?? '—'}) no tiene banco de motores: requiere fw ≥ 0.6.0.`
      : 'Robot sobre un soporte, con las dos ruedas en el aire. El control de equilibrio queda en pausa mientras el banco esté activo.';
    $<HTMLButtonElement>('motEntrar').disabled = !on || active;
    $<HTMLButtonElement>('motSalir').disabled = !active || running;
    const canDrive = active && !running && !estop;
    root.querySelectorAll<HTMLInputElement | HTMLButtonElement>('.mot-pwm input, .mot-pwm button, #motCeros').forEach((el) => (el.disabled = !canDrive));
    const rpmFw = bench?.rpmcfg !== undefined;
    root.querySelectorAll<HTMLInputElement | HTMLButtonElement>('#rpmRow input, #rpmRow button').forEach((el) => (el.disabled = !canDrive || !rpmFw));
    $<HTMLButtonElement>('rpmAplicar').disabled = !active || !rpmFw;
    segKind.querySelector<HTMLButtonElement>('[data-v="rpm"]')!.disabled = !rpmFw && on;
    const loopOn = active && last?.mode === 'rpm';
    const rpmPill = $('rpmEstado');
    rpmPill.textContent = !active ? '—' : !rpmFw ? 'requiere fw 0.7.0' : loopOn ? 'lazo activo' : 'PWM manual';
    rpmPill.className = loopOn ? 'pill ok' : 'pill';
    if (loopOn && last) {
      $('rpmMedida').textContent =
        `ref ${fmt(last.ref, 1)} · medida ${fmt(last.y, 1)} RPM · PWM ${last.pwmR} = prealim. ${fmt(last.uF, 1)} + P ${fmt(last.uP, 1)} + I ${fmt(last.uI, 1)}`;
    } else if (active) {
      $('rpmMedida').textContent = 'Mueve la referencia para activar el lazo. El izquierdo recibe el mismo PWM si está como seguidor.';
    }
    $('motManualHint').textContent = active && estop ? 'parada activa: Armar' : '';
    $<HTMLButtonElement>('motEjecutar').disabled = !canDrive || !!$('motEjecutar').dataset.invalid;
    $<HTMLButtonElement>('motDetener').disabled = !active;

    const prog = $('motProg');
    const cs = last?.cs;
    prog.hidden = !(active && last && (cs === 'running' || cs === 'done' || cs === 'aborted') && last.cn);
    if (!prog.hidden && last) {
      prog.querySelector('progress')!.value = last.cn ? last.ck / last.cn : 0;
      prog.querySelector('span')!.textContent =
        cs === 'running' ? `ensayo ${last.ci}: ${last.ck} / ${last.cn}` : `ensayo ${last.ci} ${cs === 'done' ? 'terminado' : 'abortado'} (${last.ck} muestras)`;
    }
  }

  gw.on('status', render);
  gw.on('link', render);
  gw.on('status', async (s) => {
    // Al conectar (o reconectar) se pregunta al robot por el banco
    if (s.state === 'connected' && s.bench && !bench) {
      const ack = await gw.command('bench', { op: 'status' });
      if (ack.ok) {
        bench = ack.bench as BenchStatus;
        if (bench.on) setPwmUi(bench.pwm[0], bench.pwm[1]);
        if (bench.on && bench.mode === 'rpm') setRefUi(bench.ref ?? 0);
        fillCfg(bench.rpmcfg);
        render();
      }
    }
  });
  setInterval(() => {
    renderEncoders();
    render();
  }, 200);

  fillForm();
  setView((() => {
    try {
      return localStorage.getItem('mot.vista') === 'ensayos' ? 'ensayos' : 'vivo';
    } catch {
      return 'vivo';
    }
  })());
  render();

  return {
    /** La pestaña se hizo visible: los gráficos toman su tamaño. */
    shown() {
      window.dispatchEvent(new Event('resize'));
    },
  };
}
