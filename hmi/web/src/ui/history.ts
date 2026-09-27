/**
 * Historial: sesiones guardadas en la base del PC.
 *
 * - Lista con búsqueda (notas, firmware, destino, número) y filtro por transporte.
 * - Detalle de la sesión elegida: notas editables, exportar CSV, borrar.
 *   · Gráficas: la sesión completa en las trazas (zoom arrastrando, doble clic para
 *     volver) y la escena sincronizada: al pasar el cursor por las trazas o al
 *     reproducir, el robot muestra ese instante.
 *   · Tabla: la telemetría fila a fila, por páginas; "Ir al cabezal" salta a la página
 *     del instante que muestra la escena.
 *   · Eventos: comandos, cambios de parámetros y mensajes del robot; clic = ir a ese instante.
 */
import { ApiError, api, type Gateway } from '../core/gateway';
import type { FrameKey, GwEvent, Session, TelemetryRow } from '../core/protocol';
import { Traces } from '../plots/traces';
import { RobotScene } from '../scene/scene';
import { posesFromColumns, type Pose } from '../scene/pose';
import { confirmDialog, escapeHtml } from './dialog';
import { describeEvent } from './log';

const PAGE = 100;
const SPEEDS = [0.25, 0.5, 1, 2, 4];

// Columna de la base → clave de trama que usan las trazas
const DB_TO_FRAME: Record<string, FrameKey> = {
  ang: 'ang', ref: 'ref', w: 'w', pwm: 'pwm', pwm_m: 'pwmM', rpm_l: 'rpmL', rpm_r: 'rpmR',
  kp: 'kp', dt: 'dt', u_p: 'uP', u_i: 'uI', u_d: 'uD',
};
const TABLE_COLS: [keyof TelemetryRow, string, number][] = [
  ['seq', 'seq', 0], ['st', 'estado', -1], ['ang', 'θ °', 2], ['ref', 'θ ref °', 2], ['w', 'ω °/s', 1],
  ['pwm', 'u', 1], ['pwm_m', 'u motor', 1], ['u_p', 'P', 1], ['u_i', 'I', 1], ['u_d', 'D', 1],
  ['rpm_l', 'RPM izq', 1], ['rpm_r', 'RPM der', 1], ['kp', 'Kp', 2], ['dt', 'dt ms', -2],
];

function fmtDuration(ms: number | null): string {
  if (ms === null || ms < 0) return '—';
  const s = Math.round(ms / 1000);
  const m = Math.floor(s / 60);
  const h = Math.floor(m / 60);
  return h ? `${h} h ${m % 60} min` : m ? `${m} min ${s % 60} s` : `${s} s`;
}
const fmtDate = (iso: string) => new Date(iso).toLocaleString('es-CO', { dateStyle: 'short', timeStyle: 'short' });
const fmtInt = (n: number) => n.toLocaleString('es-CO');

export function setupHistory(root: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  root.innerHTML = `
    <div class="hist">
      <aside class="hist-list">
        <div class="hist-filtros">
          <input type="text" id="hBuscar" placeholder="Buscar: notas, fw, #" aria-label="Buscar sesiones">
          <select id="hTransporte" aria-label="Transporte">
            <option value="">Todas</option><option value="serial">USB</option><option value="demo">Simulado</option>
          </select>
        </div>
        <div class="hist-items" id="hItems"></div>
      </aside>
      <section class="hist-det" id="hDet">
        <div class="hist-vacio muted">Elegir una sesión de la lista.</div>
        <div class="hist-content" hidden>
          <div class="hist-head">
            <div class="hist-title"><b id="hTitulo"></b><span class="muted" id="hMeta"></span></div>
            <div class="row">
              <input type="text" id="hNotas" placeholder="Notas de la sesión (Enter para guardar)" maxlength="200">
              <a class="btn small" id="hCsv" download>CSV</a>
              <button class="btn small" id="hBorrar" type="button">Borrar</button>
            </div>
          </div>
          <div class="seg hist-tabs" role="tablist">
            <button type="button" data-t="graf">Gráficas</button><button type="button" data-t="tabla">Tabla</button><button type="button" data-t="eventos">Eventos</button>
          </div>
          <div class="hist-pane" data-p="graf">
            <div class="hist-graf">
              <div class="escena-host" id="hEscena"></div>
              <div class="plots" id="hTrazas"></div>
            </div>
            <div class="player">
              <button class="btn small" id="hPlay" type="button" aria-label="Reproducir">▶</button>
              <select id="hVel" aria-label="Velocidad">${SPEEDS.map((s) => `<option value="${s}" ${s === 1 ? 'selected' : ''}>${s}×</option>`).join('')}</select>
              <input type="range" id="hPos" min="0" max="0" value="0" step="1" aria-label="Instante">
              <span class="mono muted" id="hT">—</span>
            </div>
            <div class="muted hist-nota" id="hNota"></div>
          </div>
          <div class="hist-pane" data-p="tabla" hidden>
            <div class="row pager">
              <button class="btn small" data-pg="first" type="button">«</button>
              <button class="btn small" data-pg="prev" type="button">‹</button>
              <span class="mono" id="hPagina">—</span>
              <button class="btn small" data-pg="next" type="button">›</button>
              <button class="btn small" data-pg="last" type="button">»</button>
              <button class="btn small" id="hIrCabezal" type="button" title="Página del instante que muestra la escena">Ir al cabezal</button>
            </div>
            <div class="tabla-wrap"><table class="tel mono" id="hTabla"></table></div>
          </div>
          <div class="hist-pane" data-p="eventos" hidden>
            <div class="log mono" id="hEventos"></div>
          </div>
        </div>
      </section>
    </div>`;

  const $ = <T extends HTMLElement = HTMLElement>(sel: string) => root.querySelector<T>(sel)!;
  const items = $('#hItems');
  const search = $<HTMLInputElement>('#hBuscar');
  const transportSel = $<HTMLSelectElement>('#hTransporte');
  const content = $('.hist-content');
  const empty = $('.hist-vacio');
  const notes = $<HTMLInputElement>('#hNotas');
  const posEl = $<HTMLInputElement>('#hPos');
  const btnPlay = $<HTMLButtonElement>('#hPlay');
  const speedEl = $<HTMLSelectElement>('#hVel');
  const tLabel = $('#hT');
  const tabBtns = [...root.querySelectorAll<HTMLButtonElement>('.hist-tabs button')];
  const panes = [...root.querySelectorAll<HTMLElement>('.hist-pane')];

  let sessions: Session[] = [];
  let current: Session | null = null;
  let times: number[] = []; // s desde el inicio de la sesión
  let poses: Pose[] = [];
  let step = 1; // tramas de la base por punto cargado
  let idx = 0;
  let playing = false;
  let page = 0;
  let tab = 'graf';
  let active = false; // pestaña Historial visible
  let loaded = false;

  const scene = new RobotScene($('#hEscena'));
  const traces = new Traces($('#hTrazas'), {
    mode: 'replay',
    onCursor: (t) => {
      if (playing || t === null) return;
      seekTime(t, false);
    },
  });

  // ------------------------------------------------------------ lista
  async function loadList() {
    try {
      sessions = await api.sessions(search.value.trim() || undefined, transportSel.value || undefined);
    } catch {
      return;
    }
    renderList();
  }

  function renderList() {
    if (!sessions.length) {
      items.innerHTML = '<div class="muted hist-vacio-lista">No hay sesiones que coincidan.</div>';
      return;
    }
    items.innerHTML = '';
    for (const s of sessions) {
      const live = s.id === gw.status?.session_id;
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'hist-item';
      b.setAttribute('aria-pressed', String(s.id === current?.id));
      b.innerHTML = `
        <span class="l1"><b>#${s.id}</b> ${fmtDate(s.started_at)} ${live ? '<span class="pill ok">en curso</span>' : ''}</span>
        <span class="l2 muted">${fmtDuration(s.t_first !== null && s.t_last !== null ? s.t_last - s.t_first : null)} · ${s.transport === 'demo' ? 'simulado' : 'USB'}${s.fw ? ` · fw ${escapeHtml(s.fw)}` : ''} · ${fmtInt(s.frames)} tramas</span>
        ${s.notes ? `<span class="l3">${escapeHtml(s.notes)}</span>` : ''}`;
      b.addEventListener('click', () => open(s.id));
      items.append(b);
    }
  }

  let searchTimer = 0;
  search.addEventListener('input', () => {
    clearTimeout(searchTimer);
    searchTimer = window.setTimeout(loadList, 250);
  });
  transportSel.addEventListener('change', loadList);

  // ------------------------------------------------------------ detalle
  async function open(id: number) {
    stop();
    let s: Session;
    try {
      s = await api.session(id);
    } catch (e) {
      log(`sesión #${id}: ${(e as Error).message}`, true);
      return;
    }
    current = s;
    renderList();
    empty.hidden = true;
    content.hidden = false;
    $('#hTitulo').textContent = `Sesión #${s.id}`;
    $('#hMeta').textContent = ` · ${fmtDate(s.started_at)} · ${fmtDuration(s.t_first !== null && s.t_last !== null ? s.t_last - s.t_first : null)} · ${s.transport === 'demo' ? 'robot simulado' : s.target ?? ''}${s.fw ? ` · fw ${s.fw}` : ''}`;
    notes.value = s.notes ?? '';
    $<HTMLAnchorElement>('#hCsv').href = api.csvUrl(s.id);
    $<HTMLButtonElement>('#hBorrar').disabled = s.id === gw.status?.session_id;
    page = 0;
    await loadData(s);
    if (tab === 'tabla') loadPage();
    if (tab === 'eventos') loadEvents();
  }

  async function loadData(s: Session) {
    $('#hNota').textContent = 'Cargando…';
    const [data, events] = await Promise.all([api.sessionColumns(s.id), api.sessionEvents(s.id)]);
    if (current?.id !== s.id) return;
    step = data.step;
    const c = data.columns;
    const t0 = (c.t_ms?.[0] as number) ?? 0;
    times = (c.t_ms ?? []).map((v) => ((v as number) - t0) / 1000);
    const cols: Partial<Record<FrameKey, (number | null)[]>> = {};
    for (const [dbKey, key] of Object.entries(DB_TO_FRAME)) cols[key] = (c[dbKey] ?? []) as (number | null)[];
    traces.load(times, cols);
    for (const ev of events) {
      if (ev.t_ms === null || !['cmd', 'reboot'].includes(ev.kind)) continue;
      if (ev.kind === 'cmd' && !ev.payload?.ok) continue;
      traces.addMarker(ev.t_ms - t0, describeEvent(ev).text);
    }
    poses = posesFromColumns(c);
    posEl.max = String(Math.max(0, times.length - 1));
    $('#hNota').textContent = data.total
      ? `${fmtInt(data.total)} tramas${step > 1 ? ` · se dibuja 1 de cada ${step} (la tabla y el CSV tienen todas)` : ''}. Arrastrar sobre las trazas para acercar; doble clic para volver. La posición del eje se integra desde las RPM.`
      : 'La sesión no tiene telemetría.';
    seekIndex(0);
    loaded = true;
  }

  async function saveNotes() {
    if (!current || (notes.value.trim() || null) === (current.notes ?? null)) return;
    try {
      current = await api.patchSession(current.id, notes.value);
      log(`notas de la sesión #${current.id} guardadas`);
    } catch (e) {
      log(`notas: ${(e as Error).message}`, true);
    }
  }
  notes.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') notes.blur();
  });
  notes.addEventListener('change', saveNotes);

  $('#hBorrar').addEventListener('click', async () => {
    if (!current) return;
    const s = current;
    const ok = await confirmDialog({
      title: `Borrar la sesión #${s.id}`,
      body: `<p>Se borran de la base del PC sus ${fmtInt(s.frames)} tramas y ${s.n_events} eventos. No se puede deshacer.</p><p>Si la vas a necesitar, exporta antes el CSV.</p>`,
      ok: 'Borrar',
      danger: true,
    });
    if (!ok) return;
    try {
      await api.deleteSession(s.id);
      log(`sesión #${s.id} borrada`);
      current = null;
      content.hidden = true;
      empty.hidden = false;
      stop();
    } catch (e) {
      log(`borrar: ${e instanceof ApiError ? e.message : (e as Error).message}`, true);
    }
  });

  // ------------------------------------------------------------ reproducción
  function seekIndex(i: number, movePlayhead = true) {
    if (!times.length) {
      scene.setPose(null);
      tLabel.textContent = '—';
      return;
    }
    idx = Math.max(0, Math.min(times.length - 1, i));
    posEl.value = String(idx);
    scene.setPose({ ...poses[idx], t: times[idx] });
    tLabel.textContent = `${times[idx].toFixed(2)} / ${times[times.length - 1].toFixed(1)} s`;
    if (movePlayhead) traces.setPlayhead(times[idx]);
  }

  function seekTime(t: number, movePlayhead = true) {
    let lo = 0;
    let hi = times.length - 1;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (times[mid] < t) lo = mid + 1;
      else hi = mid;
    }
    seekIndex(lo, movePlayhead);
  }

  posEl.addEventListener('input', () => seekIndex(Number(posEl.value)));

  let raf = 0;
  let lastWall = 0;
  let playT = 0;
  function tick(now: number) {
    if (!playing) return;
    const dt = (now - lastWall) / 1000;
    lastWall = now;
    playT += dt * Number(speedEl.value);
    if (playT >= times[times.length - 1]) {
      seekTime(times[times.length - 1]);
      stop();
      return;
    }
    seekTime(playT);
    raf = requestAnimationFrame(tick);
  }
  function play() {
    if (!times.length) return;
    if (idx >= times.length - 1) seekIndex(0);
    playing = true;
    playT = times[idx];
    lastWall = performance.now();
    btnPlay.textContent = '⏸';
    btnPlay.setAttribute('aria-label', 'Pausar');
    raf = requestAnimationFrame(tick);
  }
  function stop() {
    playing = false;
    cancelAnimationFrame(raf);
    btnPlay.textContent = '▶';
    btnPlay.setAttribute('aria-label', 'Reproducir');
  }
  btnPlay.addEventListener('click', () => (playing ? stop() : play()));

  // ------------------------------------------------------------ tabla
  async function loadPage() {
    if (!current) return;
    const pages = Math.max(1, Math.ceil(current.frames / PAGE));
    page = Math.max(0, Math.min(pages - 1, page));
    $('#hPagina').textContent = `${page + 1} / ${pages}`;
    const rows = await api.telemetryPage(current.id, page * PAGE, PAGE);
    const t0 = current.t_first ?? 0;
    const cell = (r: TelemetryRow, k: keyof TelemetryRow, d: number) => {
      const v = r[k];
      if (v === null || v === undefined) return '—';
      if (d === -1) return String(v);
      if (d === -2) return ((v as number) * 1000).toFixed(1);
      return (v as number).toFixed(d);
    };
    $('#hTabla').innerHTML =
      `<thead><tr><th>t [s]</th>${TABLE_COLS.map(([, h]) => `<th>${h}</th>`).join('')}</tr></thead><tbody>` +
      rows.map((r) => `<tr><td>${((r.t_ms - t0) / 1000).toFixed(2)}</td>${TABLE_COLS.map(([k, , d]) => `<td>${cell(r, k, d)}</td>`).join('')}</tr>`).join('') +
      '</tbody>';
  }
  root.querySelectorAll<HTMLButtonElement>('[data-pg]').forEach((b) =>
    b.addEventListener('click', () => {
      const pages = Math.ceil((current?.frames ?? 0) / PAGE);
      page = { first: 0, prev: page - 1, next: page + 1, last: pages - 1 }[b.dataset.pg as 'first'] ?? 0;
      loadPage();
    }),
  );
  $('#hIrCabezal').addEventListener('click', () => {
    page = Math.floor((idx * step) / PAGE);
    loadPage();
  });

  // ------------------------------------------------------------ eventos
  async function loadEvents() {
    if (!current) return;
    const evs = await api.sessionEvents(current.id);
    const t0 = current.t_first ?? 0;
    const box = $('#hEventos');
    if (!evs.length) {
      box.innerHTML = '<div class="vacio">Sin eventos.</div>';
      return;
    }
    box.innerHTML = '';
    for (const ev of evs as GwEvent[]) {
      const d = describeEvent(ev);
      const row = document.createElement('div');
      row.className = `ln ${d.cls}${ev.t_ms !== null ? ' click' : ''}`;
      const t = ev.t_ms !== null ? `${((ev.t_ms - t0) / 1000).toFixed(2)} s` : new Date(ev.host_ts * 1000).toLocaleTimeString('es-CO');
      row.innerHTML = `<span class="ts">${t}</span><span class="k">${escapeHtml(d.kind)}</span><span class="m">${escapeHtml(d.text)}</span>`;
      if (ev.t_ms !== null) {
        row.title = 'Ir a este instante';
        row.addEventListener('click', () => {
          setTab('graf');
          seekTime((ev.t_ms! - t0) / 1000);
        });
      }
      box.append(row);
    }
  }

  // ------------------------------------------------------------ pestañas
  function setTab(t: string) {
    tab = t;
    tabBtns.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.t === t)));
    panes.forEach((p) => (p.hidden = p.dataset.p !== t));
    if (t === 'tabla') loadPage();
    if (t === 'eventos') loadEvents();
  }
  tabBtns.forEach((b) => b.addEventListener('click', () => setTab(b.dataset.t!)));
  setTab('graf');

  gw.on('sessionsChanged', () => active && loadList());
  gw.on('status', (s) => {
    // La lista se refresca al cambiar de sesión (conectar, desconectar, reinicio)
    if (active && s.session_id !== lastSession) loadList();
    lastSession = s.session_id;
  });
  let lastSession: number | null = null;

  return {
    /** La pestaña Historial se mostró u ocultó. */
    setActive(on: boolean) {
      active = on;
      if (!on) stop();
      if (on) {
        loadList();
        if (current && !loaded) open(current.id);
      }
    },
  };
}
