/**
 * Panel de conexión: elegir transporte (USB serie o robot simulado), puerto,
 * conectar / desconectar y ver el estado del enlace. En F4 se agrega WiFi.
 *
 * Al conectar, en tablet y celular el panel se pliega a una línea de resumen para
 * dejar sitio a las trazas; "Detalles" lo despliega.
 */
import { api, type Gateway, type SerialPortInfo } from '../core/gateway';
import type { Status } from '../core/protocol';

type Kind = 'serial' | 'demo';

const STATE_LABEL: Record<Status['state'], string> = {
  disconnected: 'Desconectado',
  connecting: 'Conectando…',
  connected: 'Conectado',
};

function store(key: string, value?: string): string | null {
  try {
    if (value !== undefined) localStorage.setItem(key, value);
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export function setupConnection(root: HTMLElement, hint: HTMLElement, gw: Gateway) {
  root.innerHTML = `
    <div class="field"><span>Transporte</span>
      <div class="seg" role="group" aria-label="Transporte">
        <button type="button" data-kind="serial">USB (serie)</button>
        <button type="button" data-kind="demo">Robot simulado</button>
      </div>
    </div>
    <div class="field" data-for="serial"><span>Puerto</span>
      <div class="row">
        <select id="selPuerto" style="flex:1"></select>
        <button class="btn small" id="btnPuertos" type="button" title="Volver a buscar puertos">Buscar</button>
      </div>
    </div>
    <div class="con-resumen mono" id="conResumen"></div>
    <div class="row">
      <button class="btn primary" id="btnConectar" type="button" style="flex:1">Conectar</button>
      <button class="btn" id="btnDetalles" type="button" hidden>Detalles</button>
    </div>
    <div class="err" id="conError" hidden></div>
    <dl class="kv" id="conDatos"></dl>
    <div class="lan muted" id="conLan"></div>`;

  const segBtns = [...root.querySelectorAll<HTMLButtonElement>('.seg button')];
  const serialField = root.querySelector<HTMLElement>('[data-for="serial"]')!;
  const sel = root.querySelector<HTMLSelectElement>('#selPuerto')!;
  const btnPorts = root.querySelector<HTMLButtonElement>('#btnPuertos')!;
  const btnConnect = root.querySelector<HTMLButtonElement>('#btnConectar')!;
  const errBox = root.querySelector<HTMLElement>('#conError')!;
  const datos = root.querySelector<HTMLElement>('#conDatos')!;
  const lan = root.querySelector<HTMLElement>('#conLan')!;
  const resumen = root.querySelector<HTMLElement>('#conResumen')!;
  const btnDetails = root.querySelector<HTMLButtonElement>('#btnDetalles')!;
  const narrow = matchMedia('(max-width:1300px)');
  let compact = false;
  let lastState: Status['state'] = 'disconnected';
  btnDetails.addEventListener('click', () => {
    compact = !compact;
    render(gw.status);
  });

  async function loadInfo() {
    try {
      const { lan_urls, port } = await api.info();
      lan.innerHTML = lan_urls.length
        ? `Desde el celular (misma WiFi): ${lan_urls.map((u) => `<a class="mono" href="${u}">${u}</a>`).join(' · ')}`
        : `Sólo accesible desde este PC. Para el celular, arrancar el gateway con <span class="mono">--host 0.0.0.0</span> (puerto ${port}).`;
    } catch {
      lan.textContent = '';
    }
  }

  let kind: Kind = store('transporte') === 'demo' ? 'demo' : 'serial';
  let busy = false;

  function setKind(k: Kind) {
    kind = k;
    store('transporte', k);
    segBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.kind === k)));
    serialField.hidden = k !== 'serial';
  }
  segBtns.forEach((b) => b.addEventListener('click', () => setKind(b.dataset.kind as Kind)));
  setKind(kind);

  async function loadPorts() {
    try {
      const { serial } = await api.transports();
      const remembered = store('puerto');
      sel.innerHTML = serial.ports.length
        ? serial.ports
            .map((p: SerialPortInfo) => `<option value="${p.device}">${p.device.replace('/dev/', '')}${p.usb ? ' · USB' : ''}</option>`)
            .join('')
        : '<option value="">(no hay puertos)</option>';
      const usb = serial.ports.find((p) => p.usb);
      sel.value = serial.ports.some((p) => p.device === remembered) ? remembered! : usb?.device ?? serial.ports[0]?.device ?? '';
    } catch (e) {
      showError(`No se pudo consultar el gateway: ${(e as Error).message}`);
    }
  }
  btnPorts.addEventListener('click', loadPorts);
  sel.addEventListener('change', () => store('puerto', sel.value));

  function showError(msg: string | null) {
    errBox.hidden = !msg;
    errBox.textContent = msg ?? '';
  }

  btnConnect.addEventListener('click', async () => {
    busy = true;
    showError(null);
    render(gw.status);
    try {
      if (gw.status?.state === 'disconnected' || !gw.status) {
        if (kind === 'serial' && !sel.value) throw new Error('Elegir un puerto.');
        await api.connect(kind, kind === 'serial' ? sel.value : undefined);
      } else {
        await api.disconnect();
      }
    } catch (e) {
      showError((e as Error).message);
    } finally {
      busy = false;
      render(gw.status);
    }
  });

  function render(s: Status | null) {
    const state = s?.state ?? 'disconnected';
    const on = state !== 'disconnected';
    if (state !== lastState) {
      if (state === 'connected') compact = narrow.matches;
      if (state === 'disconnected') compact = false;
      lastState = state;
    }
    root.classList.toggle('compact', on && compact);
    btnDetails.hidden = !on;
    btnDetails.textContent = compact ? 'Detalles' : 'Ocultar';
    resumen.textContent = on && s
      ? `${s.target?.replace('/dev/', '') ?? '—'} · fw ${s.fw ?? '—'} · ${s.rate.toFixed(1)} Hz · sesión #${s.session_id ?? '—'}`
      : '';
    btnConnect.textContent = busy ? 'Espere…' : on ? 'Desconectar' : 'Conectar';
    btnConnect.classList.toggle('primary', !on);
    btnConnect.disabled = busy || !gw.linkUp;
    segBtns.forEach((b) => (b.disabled = on));
    // Conectado: el selector muestra el transporte real, no la última elección guardada
    const shown = on && s?.transport ? s.transport : kind;
    segBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.kind === shown)));
    serialField.hidden = shown !== 'serial';
    sel.disabled = btnPorts.disabled = on;
    hint.innerHTML = gw.linkUp
      ? `<span class="pill ${state === 'connected' ? 'ok' : state === 'connecting' ? 'warn' : ''}">${STATE_LABEL[state]}</span>`
      : '<span class="pill mal">Sin gateway</span>';
    if (s?.error && !busy && state === 'disconnected') showError(s.error);

    const rows: [string, string][] = [];
    if (on && s) {
      rows.push(['Destino', s.target ?? '—']);
      rows.push(['Firmware', s.fw ? `${s.fw} · protocolo v${s.proto}` : '—']);
      rows.push(['Parámetros', s.params_src === 'nvs' ? 'guardados en el robot' : s.params_src === 'factory' ? 'de fábrica' : '—']);
      rows.push(['Sesión', s.session_id ? `#${s.session_id}` : '—']);
      rows.push(['Frecuencia', `${s.rate.toFixed(1)} Hz`]);
      rows.push(['Tramas perdidas', String(s.gaps)]);
    }
    if (s) rows.push(['Pantallas conectadas', String(s.clients)]);
    datos.innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd class="mono">${v}</dd>`).join('');
  }

  gw.on('status', render);
  gw.on('link', (up) => {
    render(gw.status);
    if (up) {
      loadPorts();
      loadInfo();
    }
  });
  render(null);
}
