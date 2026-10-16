/**
 * Tarjeta Conducir (M4, estructura RpmCascade): botones en pantalla y flechas del teclado.
 *
 * - Mientras se mantenga pulsado un botón o una flecha, se envía `drive` {v, r} cada 200 ms:
 *   el robot lo vence a 1 s sin renovar, así que si se corta el enlace se detiene en su sitio.
 * - Al soltar todo, se envía {v: 0, r: 0} una vez.
 * - Arriba/abajo: velocidad ±v (deslizador «Velocidad»); izquierda/derecha: giro ±r
 *   («Giro»). Se pueden combinar (avanzar girando).
 * - «Volver al inicio»: el robot regresa a donde se armó (x_ref = 0).
 * - Sólo se habilita con el robot conectado, en RpmCascade y controlando.
 */
import type { Gateway } from '../core/gateway';
import type { Frame } from '../core/protocol';

type Dir = 'up' | 'down' | 'left' | 'right';

const RENEW_MS = 200;
const KEYS: Record<string, Dir> = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };

function store(key: string, value?: string): string | null {
  try {
    if (value !== undefined) localStorage.setItem(key, value);
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export function setupDrive(root: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  root.innerHTML = `
    <div class="drv-estado muted" id="drvEstado"></div>
    <div class="drv">
      <div class="drv-pad">
        <span></span><button class="btn drv-b" type="button" data-d="up" aria-label="Adelante">▲</button><span></span>
        <button class="btn drv-b" type="button" data-d="left" aria-label="Girar a la izquierda">◀</button>
        <button class="btn drv-b drv-stop" type="button" id="drvStop" aria-label="Detener">■</button>
        <button class="btn drv-b" type="button" data-d="right" aria-label="Girar a la derecha">▶</button>
        <span></span><button class="btn drv-b" type="button" data-d="down" aria-label="Atrás">▼</button><span></span>
      </div>
      <div class="drv-ctl">
        <label class="field"><span>Velocidad <b class="mono" id="drvVval"></b></span><input type="range" id="drvV" min="2" max="20" step="1"></label>
        <label class="field"><span>Giro <b class="mono" id="drvRval"></b></span><input type="range" id="drvR" min="5" max="30" step="1"></label>
        <button class="btn small" type="button" id="drvHome">Volver al inicio</button>
      </div>
    </div>
    <div class="drv-vivo mono" id="drvVivo">—</div>
    <div class="mot-nota muted">Mantener pulsado un botón o una flecha del teclado. Al soltar, el robot se detiene; si se corta el enlace, también (a 1 s).</div>`;

  const $ = <T extends HTMLElement = HTMLElement>(id: string) => root.querySelector<T>('#' + id)!;
  const vIn = $<HTMLInputElement>('drvV');
  const rIn = $<HTMLInputElement>('drvR');
  const held = new Set<Dir>();
  let timer = 0;
  let last: Frame | null = null;

  vIn.value = store('drive.v') ?? '10';
  rIn.value = store('drive.r') ?? '20';

  const enabled = () =>
    gw.linkUp && gw.status?.state === 'connected' && gw.params?.structure === 'RpmCascade' && gw.status?.robot_state === 'ACTIVE';

  function command(): { v: number; r: number } {
    const v = Number(vIn.value);
    const r = Number(rIn.value);
    return {
      v: (held.has('up') ? v : 0) - (held.has('down') ? v : 0),
      r: (held.has('left') ? r : 0) - (held.has('right') ? r : 0),
    };
  }

  async function send(fields: Record<string, unknown>) {
    const ack = await gw.command('drive', fields);
    if (!ack.ok) log(`conducir: ${ack.err}`, true);
  }

  function update() {
    root.querySelectorAll<HTMLButtonElement>('[data-d]').forEach((b) => b.setAttribute('aria-pressed', String(held.has(b.dataset.d as Dir))));
    if (held.size && enabled()) {
      send(command());
      if (!timer) timer = window.setInterval(() => (held.size && enabled() ? send(command()) : release()), RENEW_MS);
    } else if (!held.size) {
      release();
    }
  }

  function release() {
    if (timer) {
      clearInterval(timer);
      timer = 0;
      if (gw.linkUp && gw.status?.state === 'connected') send({ v: 0, r: 0 });
    }
    held.clear();
    root.querySelectorAll<HTMLButtonElement>('[data-d]').forEach((b) => b.setAttribute('aria-pressed', 'false'));
  }

  // ------------------------------------------------------------ botones (mantener pulsado)
  root.querySelectorAll<HTMLButtonElement>('[data-d]').forEach((b) => {
    const d = b.dataset.d as Dir;
    b.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      b.setPointerCapture(e.pointerId);
      held.add(d);
      update();
    });
    const up = () => {
      held.delete(d);
      update();
    };
    b.addEventListener('pointerup', up);
    b.addEventListener('pointercancel', up);
    b.addEventListener('lostpointercapture', up);
    b.addEventListener('contextmenu', (e) => e.preventDefault()); // pulsación larga en táctil
  });
  $('drvStop').addEventListener('click', () => {
    held.clear();
    release();
    send({ v: 0, r: 0 });
  });
  $('drvHome').addEventListener('click', () => {
    release();
    send({ home: true });
  });

  // ------------------------------------------------------------ flechas del teclado
  const typing = (t: EventTarget | null) =>
    t instanceof HTMLElement && (t.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(t.tagName) && !(t instanceof HTMLInputElement && t.type === 'range'));
  document.addEventListener('keydown', (e) => {
    const d = KEYS[e.key];
    if (!d || typing(e.target) || document.querySelector('dialog[open]') || !enabled()) return;
    e.preventDefault(); // sin desplazar la página
    if (e.repeat || held.has(d)) return;
    held.add(d);
    update();
  });
  document.addEventListener('keyup', (e) => {
    const d = KEYS[e.key];
    if (!d || !held.has(d)) return;
    held.delete(d);
    update();
  });
  window.addEventListener('blur', () => held.size && release()); // se cambia de ventana con una flecha pulsada

  // ------------------------------------------------------------ deslizadores y estado
  function labels() {
    $('drvVval').textContent = `${vIn.value} RPM (${((Number(vIn.value) / 60) * 2 * Math.PI * 3.25).toFixed(1)} cm/s)`;
    $('drvRval').textContent = `${rIn.value} °/s`;
  }
  vIn.addEventListener('input', () => {
    store('drive.v', vIn.value);
    labels();
  });
  rIn.addEventListener('input', () => {
    store('drive.r', rIn.value);
    labels();
  });

  function render() {
    const on = enabled();
    root.querySelectorAll<HTMLButtonElement>('.drv-b, #drvHome').forEach((b) => (b.disabled = !on));
    const p = gw.params;
    if (p) {
      vIn.max = String(p.v_ref_max ?? 20);
      rIn.max = String(p.r_ref_max ?? 30);
    }
    let msg = '';
    if (!gw.linkUp || gw.status?.state !== 'connected') msg = 'Conectar el robot.';
    else if (p?.structure !== 'RpmCascade') msg = 'Disponible con la estructura RpmCascade (fw ≥ 0.9.0).';
    else if (gw.status?.robot_state !== 'ACTIVE') msg = 'Poner el robot vertical para que controle.';
    $('drvEstado').textContent = msg;
    $('drvEstado').hidden = !msg;
    if (!on && held.size) release();
    labels();
  }

  gw.on('frames', (fs) => {
    last = fs[fs.length - 1] ?? last;
    if (last?.x !== undefined) {
      $('drvVivo').textContent =
        `posición ${last.x?.toFixed(1)} cm (ref ${last.xr?.toFixed(1)}) · v pedida ${last.vr?.toFixed(1)} RPM · rumbo ${last.psi?.toFixed(1)}° (ref ${last.psir?.toFixed(1)})`;
    }
  });
  gw.on('status', render);
  gw.on('params', render);
  gw.on('link', render);
  render();
}
