/**
 * Tarjeta "WiFi del robot": estado de la radio del ESP32 y cambio de modo.
 *
 * - Red propia (AP, por defecto): el robot crea "Balancin-XXXX" con clave única; el PC y el
 *   celular se unen a ella y el gateway se conecta a 192.168.4.1.
 * - Red local (STA): el robot se une a un router; si no lo logra en 15 s vuelve a su red
 *   propia para no quedar inaccesible.
 * - Apagado.
 * Funciona con el robot conectado por USB o por WiFi (comando "wifi" del protocolo).
 * La clave de la red local nunca vuelve del robot: sólo se sabe si está guardada.
 */
import type { Gateway } from '../core/gateway';
import { confirmDialog, escapeHtml } from './dialog';

interface WifiStatus {
  mode: 'ap' | 'sta' | 'off';
  active: 'ap' | 'sta' | 'sta_connecting' | 'ap_fallback' | 'off';
  hostname: string;
  ap_ssid: string;
  ap_pass: string;
  ap_pass_default: boolean;
  ssid: string;
  pass_set: boolean;
  ip?: string;
  rssi?: number;
  clients: number;
}

const ACTIVE_LABEL: Record<WifiStatus['active'], string> = {
  ap: 'Red propia',
  sta: 'Red local',
  sta_connecting: 'Conectando a la red local…',
  ap_fallback: 'Red propia (no pudo unirse a la red local)',
  off: 'Apagado',
};

export function setupWifi(root: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  root.innerHTML = `
    <div class="muted wifi-vacio">Conectar el robot (por USB o WiFi) para ver su WiFi.</div>
    <div class="wifi-body" hidden>
      <dl class="kv" id="wfDatos"></dl>
      <details class="grupo" id="wfCambiar">
        <summary>Cambiar la red</summary>
        <div class="grupo-body">
          <div class="seg wifi-modos" role="group" aria-label="Modo WiFi">
            <button type="button" data-m="ap">Red propia</button><button type="button" data-m="sta">Red local</button><button type="button" data-m="off">Apagado</button>
          </div>
          <div class="field" data-para="sta"><span>Red (SSID)</span><input type="text" id="wfSsid" maxlength="32" autocapitalize="off" autocorrect="off" spellcheck="false"></div>
          <div class="field" data-para="sta"><span>Clave de la red</span><input type="password" id="wfPass" maxlength="63" autocomplete="off" placeholder="(sin cambios)"></div>
          <div class="field" data-para="ap"><span>Clave de la red propia</span><input type="text" id="wfApPass" maxlength="63" autocomplete="off" placeholder="vacía = clave del robot"></div>
          <div class="muted wifi-nota" id="wfNota"></div>
          <button class="btn primary" type="button" id="wfAplicar">Guardar y aplicar</button>
        </div>
      </details>
    </div>`;

  const $ = <T extends HTMLElement = HTMLElement>(s: string) => root.querySelector<T>(s)!;
  const body = $('.wifi-body');
  const empty = $('.wifi-vacio');
  const datos = $('#wfDatos');
  const modeBtns = [...root.querySelectorAll<HTMLButtonElement>('.wifi-modos button')];
  const ssid = $<HTMLInputElement>('#wfSsid');
  const pass = $<HTMLInputElement>('#wfPass');
  const apPass = $<HTMLInputElement>('#wfApPass');
  const nota = $('#wfNota');
  let status: WifiStatus | null = null;
  let mode: WifiStatus['mode'] = 'ap';
  let showPass = false;

  function setMode(m: WifiStatus['mode']) {
    mode = m;
    modeBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.m === m)));
    root.querySelectorAll<HTMLElement>('[data-para]').forEach((el) => (el.hidden = el.dataset.para !== m));
    nota.textContent =
      m === 'sta' ? 'Si en 15 s no se une a la red, el robot vuelve a su red propia. PC y celular deben estar en esa misma red.'
      : m === 'off' ? 'Sin WiFi sólo se podrá conectar por USB.'
      : 'PC y celular se unen a la red del robot; el gateway se conecta a 192.168.4.1.';
  }
  modeBtns.forEach((b) => b.addEventListener('click', () => setMode(b.dataset.m as WifiStatus['mode'])));

  function render() {
    const on = gw.linkUp && gw.status?.state === 'connected';
    empty.hidden = on && !!status;
    body.hidden = !(on && status);
    if (!status) return;
    const s = status;
    const apOn = s.active === 'ap' || s.active === 'ap_fallback';
    const rows: [string, string][] = [['Estado', escapeHtml(ACTIVE_LABEL[s.active] ?? s.active)]];
    if (apOn) {
      rows.push(['Red', escapeHtml(s.ap_ssid)]);
      rows.push(['Clave', `<span class="mono">${showPass ? escapeHtml(s.ap_pass) : '••••••••'}</span> <button class="btn small" type="button" id="wfVer">${showPass ? 'Ocultar' : 'Ver'}</button>`]);
    } else if (s.mode === 'sta') {
      rows.push(['Red', escapeHtml(s.ssid)]);
    }
    if (s.ip) rows.push(['Dirección', `${escapeHtml(s.ip)} · ${escapeHtml(s.hostname)}`]);
    if (s.rssi !== undefined) rows.push(['Señal', `${s.rssi} dBm`]);
    rows.push(['Clientes WiFi', String(s.clients)]);
    datos.innerHTML = rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
    root.querySelector('#wfVer')?.addEventListener('click', () => {
      showPass = !showPass;
      render();
    });
  }

  async function refresh() {
    if (!(gw.linkUp && gw.status?.state === 'connected')) {
      status = null;
      render();
      return;
    }
    const ack = await gw.command('wifi');
    if (!ack.ok) {
      // Firmware anterior a 0.4.0: no tiene WiFi
      status = null;
      empty.textContent = ack.err?.includes('desconocido') ? 'Este firmware no tiene WiFi (requiere ≥ 0.4.0).' : `WiFi: ${ack.err}`;
      render();
      return;
    }
    status = ack.wifi as WifiStatus;
    ssid.value = status.ssid;
    apPass.value = status.ap_pass_default ? '' : status.ap_pass;
    pass.value = '';
    setMode(status.mode);
    render();
  }

  $('#wfAplicar').addEventListener('click', async () => {
    const set: Record<string, string> = { mode };
    if (mode === 'sta') {
      if (!ssid.value.trim()) return log('WiFi: falta la red (SSID)', true);
      set.ssid = ssid.value.trim();
      if (pass.value) set.pass = pass.value;
    }
    if (mode === 'ap') set.ap_pass = apPass.value;
    const viaWifi = gw.status?.transport === 'wifi';
    const target = mode === 'ap' ? `la red «${escapeHtml(status?.ap_ssid ?? '')}» y conectar a 192.168.4.1` : mode === 'sta' ? `la red «${escapeHtml(set.ssid)}» y conectar a ${escapeHtml(status?.hostname ?? 'la IP del robot')}` : 'USB';
    const ok = await confirmDialog({
      title: 'Cambiar el WiFi del robot',
      body: `<p>La configuración queda guardada en el robot y se aplica en medio segundo. El control no se detiene.</p>${
        viaWifi ? `<p><b>Estás conectado por WiFi: la conexión se va a cortar.</b> El gateway reintenta solo; si la dirección cambia, desconectar y usar ${target}.</p>` : `<p>Para usarla: ${target}.</p>`
      }`,
      ok: 'Guardar y aplicar',
    });
    if (!ok) return;
    const ack = await gw.command('wifi', { set });
    if (!ack.ok) return log(`WiFi: ${ack.err}`, true);
    log('WiFi del robot actualizado');
    ($('#wfCambiar') as HTMLDetailsElement).open = false;
    status = ack.wifi as WifiStatus;
    render();
    setTimeout(refresh, 4000); // el estado real (IP, conexión) llega unos segundos después
  });

  let lastState = '';
  gw.on('status', (s) => {
    if (s.state !== lastState) {
      lastState = s.state;
      refresh();
    }
  });
  gw.on('link', () => refresh());
  setMode('ap');
}
