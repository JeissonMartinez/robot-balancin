/**
 * Panel de conexión: elegir transporte (USB serie, WiFi o robot simulado), puerto o
 * dirección, conectar / desconectar y ver el estado del enlace. Por WiFi, si la conexión
 * se pierde el gateway reintenta solo; "Cancelar" detiene los reintentos.
 *
 * Al conectar, en tablet y celular el panel se pliega a una línea de resumen para
 * dejar sitio a las trazas; "Detalles" lo despliega.
 */
import { api, type Gateway, type SerialPortInfo } from '../core/gateway';
import type { Status } from '../core/protocol';

type Kind = 'serial' | 'wifi' | 'mqtt' | 'demo';

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
        <button type="button" data-kind="serial">USB</button>
        <button type="button" data-kind="wifi">WiFi</button>
        <button type="button" data-kind="mqtt">MQTT</button>
        <button type="button" data-kind="demo">Simulado</button>
      </div>
    </div>
    <div class="field" data-for="serial"><span>Puerto</span>
      <div class="row">
        <select id="selPuerto" style="flex:1"></select>
        <button class="btn small" id="btnPuertos" type="button" title="Volver a buscar puertos">Buscar</button>
      </div>
    </div>
    <div class="con-resumen mono" id="conResumen"></div>
    <div class="field" data-for="wifi"><span>Dirección del robot</span>
      <input type="text" id="inHost" placeholder="192.168.4.1" autocapitalize="off" autocorrect="off" spellcheck="false">
      <div class="muted con-ayuda">Red propia del robot: 192.168.4.1 · En la red local: su IP o su nombre .local (ver «WiFi del robot»).</div>
    </div>
    <div class="field" data-for="mqtt"><span>Broker MQTT</span>
      <input type="text" id="inBroker" placeholder="127.0.0.1" autocapitalize="off" autocorrect="off" spellcheck="false">
    </div>
    <div class="field" data-for="mqtt"><span>Robot</span>
      <div class="row">
        <select id="selRobot" style="flex:1"><option value="">(buscar en el broker)</option></select>
        <button class="btn small" id="btnRobots" type="button">Buscar</button>
      </div>
      <div class="muted con-ayuda">El robot debe tener MQTT activo apuntando a este broker (tarjeta «WiFi del robot»).</div>
    </div>
    <div class="row">
      <button class="btn primary" id="btnConectar" type="button" style="flex:1">Conectar</button>
      <button class="btn" id="btnDetalles" type="button" hidden>Detalles</button>
    </div>
    <div class="err" id="conError" hidden></div>
    <dl class="kv" id="conDatos"></dl>
    <div class="lan muted" id="conLan"></div>`;

  const segBtns = [...root.querySelectorAll<HTMLButtonElement>('.seg button')];
  const serialField = root.querySelector<HTMLElement>('[data-for="serial"]')!;
  const wifiField = root.querySelector<HTMLElement>('[data-for="wifi"]')!;
  const mqttFields = [...root.querySelectorAll<HTMLElement>('[data-for="mqtt"]')];
  const broker = root.querySelector<HTMLInputElement>('#inBroker')!;
  const selRobot = root.querySelector<HTMLSelectElement>('#selRobot')!;
  const btnRobots = root.querySelector<HTMLButtonElement>('#btnRobots')!;
  broker.value = store('broker') ?? '127.0.0.1';
  broker.addEventListener('change', () => store('broker', broker.value.trim()));
  const savedRobot = store('robotMqtt');
  if (savedRobot) selRobot.innerHTML = `<option value="${savedRobot}">${savedRobot}</option>`;
  btnRobots.addEventListener('click', async () => {
    showError(null);
    try {
      const list = await api.mqttRobots(broker.value.trim() || '127.0.0.1');
      selRobot.innerHTML = list.length
        ? list.map((r) => `<option value="${r.robot}" ${r.status !== 'online' ? 'disabled' : ''}>${r.robot}${r.status !== 'online' ? ' (desconectado)' : ''}</option>`).join('')
        : '<option value="">(ningún robot en el broker)</option>';
      const online = list.find((r) => r.status === 'online');
      if (online) selRobot.value = online.robot;
    } catch (e) {
      showError((e as Error).message);
    }
  });
  const host = root.querySelector<HTMLInputElement>('#inHost')!;
  host.value = store('robotHost') ?? '192.168.4.1';
  host.addEventListener('change', () => store('robotHost', host.value.trim()));
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

  const savedKind = store('transporte');
  let kind: Kind = savedKind === 'demo' || savedKind === 'wifi' || savedKind === 'mqtt' ? savedKind : 'serial';
  let busy = false;

  function setKind(k: Kind) {
    kind = k;
    store('transporte', k);
    segBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.kind === k)));
    serialField.hidden = k !== 'serial';
    wifiField.hidden = k !== 'wifi';
    mqttFields.forEach((f) => (f.hidden = k !== 'mqtt'));
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
      if (gw.status?.retrying) {
        await api.disconnect(); // cancela los reintentos
      } else if (gw.status?.state === 'disconnected' || !gw.status) {
        if (kind === 'serial' && !sel.value) throw new Error('Elegir un puerto.');
        if (kind === 'wifi' && !host.value.trim()) throw new Error('Escribir la dirección del robot.');
        if (kind === 'mqtt' && !selRobot.value) throw new Error('Elegir el robot (Buscar).');
        store('robotHost', host.value.trim());
        if (kind === 'mqtt') {
          store('broker', broker.value.trim());
          store('robotMqtt', selRobot.value);
        }
        await api.connect(
          kind,
          kind === 'serial' ? { port: sel.value }
          : kind === 'wifi' ? { host: host.value.trim() }
          : kind === 'mqtt' ? { host: broker.value.trim() || '127.0.0.1', robot: selRobot.value }
          : {},
        );
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
    const retrying = !!s?.retrying && !on;
    btnConnect.textContent = busy ? 'Espere…' : retrying ? 'Cancelar reconexión' : on ? 'Desconectar' : 'Conectar';
    btnConnect.classList.toggle('primary', !on && !retrying);
    btnConnect.disabled = busy || !gw.linkUp;
    segBtns.forEach((b) => (b.disabled = on || retrying));
    host.disabled = broker.disabled = selRobot.disabled = btnRobots.disabled = on || retrying;
    // Conectado: el selector muestra el transporte real, no la última elección guardada
    const shown = on && s?.transport ? s.transport : kind;
    segBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.kind === shown)));
    serialField.hidden = shown !== 'serial';
    wifiField.hidden = shown !== 'wifi';
    mqttFields.forEach((f) => (f.hidden = shown !== 'mqtt'));
    sel.disabled = btnPorts.disabled = on;
    hint.innerHTML = gw.linkUp
      ? `<span class="pill ${state === 'connected' ? 'ok' : state === 'connecting' ? 'warn' : ''}">${STATE_LABEL[state]}</span>`
      : '<span class="pill mal">Sin gateway</span>';
    if (s?.error && !busy && state === 'disconnected') showError(s.error);
    else if (on) showError(null);

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
