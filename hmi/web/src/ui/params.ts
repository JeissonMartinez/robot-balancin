/**
 * Panel de parámetros: un control por parámetro del firmware, construido desde el
 * `schema` que envía el robot.
 *
 * - Números: slider + campo numérico + unidad; botón ↺ para el valor de fábrica.
 * - enum / signo / booleano: botones de opción; enteros con nombre: selector.
 * - Los cambios quedan "pendientes" (borde dorado). En vivo (por defecto) se envían al
 *   soltar el slider o confirmar el número; el control destella en verde al confirmarlo
 *   el robot y la cabecera de la tarjeta resume el último cambio (sin mover nada de
 *   lugar). Sin "en vivo" se acumulan y se envían juntos con Aplicar, en un solo `set`
 *   (todo o nada, útil para kp_min y kp_max a la vez); la barra de Aplicar queda fija
 *   mientras dure ese modo.
 * - Lo que el robot confirma llega a todas las pantallas; un valor que alguien está
 *   editando no se pisa.
 */
import type { Gateway } from '../core/gateway';
import type { ParamDesc, Params } from '../core/protocol';
import {
  GROUPS, decimalsFor, formatValue, sameValue, stepFor, uiFor, uiOrder, validate,
} from '../core/paramspec';
import { confirmDialog, escapeHtml } from './dialog';

type Value = Params[string];

interface Control {
  d: ParamDesc;
  root: HTMLElement;
  set(v: Value): void; // muestra un valor
  enable(on: boolean): void;
}

function store(key: string, value?: string): string | null {
  try {
    if (value !== undefined) localStorage.setItem(key, value);
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}

export function setupParams(root: HTMLElement, headExtra: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  let schema: ParamDesc[] = [];
  let robot: Params = {};
  const pending = new Map<string, Value>();
  const controls = new Map<string, Control>();
  let live = store('params.vivo') !== 'no';
  let busy = false;

  headExtra.innerHTML = `
    <span class="par-estado" id="parEstado" aria-live="polite"></span>
    <label class="switch" title="Enviar cada cambio al soltar el slider o confirmar el número">
      <input type="checkbox" id="chkVivo"> En vivo
    </label>`;
  const chkLive = headExtra.querySelector<HTMLInputElement>('#chkVivo')!;
  const status = headExtra.querySelector<HTMLElement>('#parEstado')!;
  const rejected = new Map<string, string>(); // clave → motivo del último rechazo
  function setStatus(text: string, bad = false) {
    status.textContent = text;
    status.classList.toggle('bad', bad);
    status.title = text;
  }
  chkLive.checked = live;
  chkLive.addEventListener('change', () => {
    live = chkLive.checked;
    store('params.vivo', live ? 'si' : 'no');
    renderBar();
  });

  root.innerHTML = `
    <div class="params-empty muted">Conectar el robot para ver sus parámetros.</div>
    <div class="params-bar" hidden>
      <span class="params-msg" id="parMsg"></span>
      <button class="btn small" id="btnDescartar" type="button">Descartar</button>
      <button class="btn small primary" id="btnAplicar" type="button">Aplicar</button>
    </div>
    <div class="params-groups" id="parGrupos"></div>
    <div class="params-foot" hidden>
      <button class="btn small" id="btnReleer" type="button" title="Volver a leer los parámetros del robot">Releer</button>
      <button class="btn small" id="btnFabrica" type="button">Valores de fábrica</button>
      <button class="btn small" id="btnGuardarNvs" type="button">Guardar en el robot</button>
    </div>`;

  const empty = root.querySelector<HTMLElement>('.params-empty')!;
  const bar = root.querySelector<HTMLElement>('.params-bar')!;
  const msg = root.querySelector<HTMLElement>('#parMsg')!;
  const groupsEl = root.querySelector<HTMLElement>('#parGrupos')!;
  const foot = root.querySelector<HTMLElement>('.params-foot')!;
  const btnApply = root.querySelector<HTMLButtonElement>('#btnAplicar')!;
  const btnDiscard = root.querySelector<HTMLButtonElement>('#btnDescartar')!;

  const connected = () => gw.linkUp && gw.status?.state === 'connected';
  const structure = () => (pending.get('structure') ?? robot.structure) as string | undefined;

  // ------------------------------------------------------------ construcción
  function build() {
    controls.clear();
    groupsEl.innerHTML = '';
    const byGroup = new Map<string, ParamDesc[]>();
    for (const d of [...schema].sort((a, b) => uiOrder(a.key) - uiOrder(b.key))) {
      const g = uiFor(d).group;
      byGroup.set(g, [...(byGroup.get(g) ?? []), d]);
    }
    for (const g of GROUPS) {
      const items = byGroup.get(g);
      if (!items) continue;
      const det = document.createElement('details');
      det.className = 'grupo';
      det.open = store(`params.grupo.${g}`) !== 'cerrado';
      det.addEventListener('toggle', () => store(`params.grupo.${g}`, det.open ? 'abierto' : 'cerrado'));
      det.innerHTML = `<summary>${g}<span class="n"></span></summary><div class="grupo-body"></div>`;
      const body = det.querySelector<HTMLElement>('.grupo-body')!;
      for (const d of items) {
        const c = makeControl(d);
        controls.set(d.key, c);
        body.append(c.root);
      }
      groupsEl.append(det);
    }
    refresh();
  }

  function makeControl(d: ParamDesc): Control {
    const ui = uiFor(d);
    const el = document.createElement('div');
    el.className = 'ctl';
    el.dataset.key = d.key;
    const reset = `<button class="reset" type="button" title="Valor de fábrica: ${escapeHtml(formatValue(d, d.def))}" aria-label="Valor de fábrica">↺</button>`;
    const head = `<span class="name"><span class="lbl">${escapeHtml(ui.label)}</span> <span class="key mono">${d.key}</span></span>`;
    const note = `<span class="nota">${escapeHtml(d.desc)}<span class="only"></span><span class="err-msg"></span></span>`;

    if (d.type === 'float' || (d.type === 'int' && !ui.options)) {
      const step = stepFor(d);
      el.innerHTML = `${head}
        <input class="num mono" type="number" inputmode="decimal" step="any" min="${d.min}" max="${d.max}" aria-label="${escapeHtml(ui.label)}">
        <span class="uni">${escapeHtml(d.unit)}</span>${reset}
        <input type="range" min="${d.min}" max="${d.max}" step="${step}" aria-label="${escapeHtml(ui.label)}">
        ${note}`;
      const num = el.querySelector<HTMLInputElement>('.num')!;
      const range = el.querySelector<HTMLInputElement>('input[type=range]')!;
      const dec = decimalsFor(d);
      range.addEventListener('input', () => {
        num.value = Number(range.value).toFixed(dec);
        stage(d.key, Number(range.value), false);
      });
      range.addEventListener('change', () => stage(d.key, Number(range.value), true));
      num.addEventListener('change', () => {
        const v = Number(num.value);
        if (num.value.trim() === '' || !isFinite(v)) return refresh();
        stage(d.key, d.type === 'int' ? Math.round(v) : v, true);
      });
      num.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') num.blur();
      });
      bindReset(el, d);
      return {
        d, root: el,
        set(v) {
          if (typeof v !== 'number') return;
          if (document.activeElement !== num) num.value = v.toFixed(dec);
          range.value = String(v);
        },
        enable(on) {
          num.disabled = range.disabled = !on;
        },
      };
    }

    if (d.type === 'int' && ui.options) {
      const opts = Object.entries(ui.options).map(([v, t]) => `<option value="${v}">${escapeHtml(t)}</option>`).join('');
      el.innerHTML = `${head}<select class="sel" aria-label="${escapeHtml(ui.label)}">${opts}</select>${reset}${note}`;
      const sel = el.querySelector<HTMLSelectElement>('select')!;
      sel.addEventListener('change', () => stage(d.key, Number(sel.value), true));
      bindReset(el, d);
      return {
        d, root: el,
        set(v) { sel.value = String(v); },
        enable(on) { sel.disabled = !on; },
      };
    }

    // enum, sign, bool: botones de opción
    const choices: [Value, string][] =
      d.type === 'enum' ? (d.options ?? []).map((o) => [o, o] as [Value, string])
      : d.type === 'sign' ? [[-1, '−1'], [1, '+1']]
      : [[true, 'Sí'], [false, 'No']];
    el.innerHTML = `${head}<div class="seg opts" role="group" aria-label="${escapeHtml(ui.label)}">${choices
      .map(([, t], i) => `<button type="button" data-i="${i}">${escapeHtml(t)}</button>`).join('')}</div>${reset}${note}`;
    const btns = [...el.querySelectorAll<HTMLButtonElement>('.opts button')];
    btns.forEach((b, i) => b.addEventListener('click', () => stage(d.key, choices[i][0], true)));
    bindReset(el, d);
    return {
      d, root: el,
      set(v) { btns.forEach((b, i) => b.setAttribute('aria-pressed', String(sameValue(choices[i][0], v)))); },
      enable(on) { btns.forEach((b) => (b.disabled = !on)); },
    };
  }

  function bindReset(el: HTMLElement, d: ParamDesc) {
    el.querySelector<HTMLButtonElement>('.reset')!.addEventListener('click', () => stage(d.key, d.def, true));
  }

  // ------------------------------------------------------------ cambios
  function stage(key: string, v: Value, commit: boolean) {
    rejected.delete(key);
    if (sameValue(robot[key], v)) pending.delete(key);
    else pending.set(key, v);
    refresh();
    if (commit && live && pending.has(key)) apply([key]);
  }

  async function apply(keys = [...pending.keys()]) {
    if (!keys.length || busy) return;
    const changes: Params = {};
    for (const k of keys) changes[k] = pending.get(k)!;
    const errs = validate(schema, { ...robot, ...Object.fromEntries(pending) });
    const bad = keys.filter((k) => errs[k]);
    if (bad.length) {
      setStatus(`No se envió: ${errs[bad[0]]}`, true);
      return;
    }
    busy = true;
    renderBar();
    const ack = await gw.command('set', { params: changes });
    busy = false;
    if (ack.ok) {
      keys.forEach((k) => pending.delete(k));
      if (ack.params) robot = ack.params;
      const d = schema.find((x) => x.key === keys[0]);
      setStatus(keys.length === 1 && d ? `${keys[0]} = ${formatValue(d, robot[keys[0]])} ✓` : `${keys.length} cambios aplicados ✓`);
      refresh();
      for (const k of keys) flash(k);
    } else {
      for (const k of keys) rejected.set(k, ack.err ?? 'rechazado');
      setStatus(`Rechazado: ${ack.err}`, true);
      log(`set: ${ack.err}`, true);
      refresh();
    }
  }

  /** Destello verde en el control confirmado por el robot (no cambia el tamaño de nada). */
  function flash(key: string) {
    const el = controls.get(key)?.root;
    if (!el) return;
    el.classList.remove('aplicado');
    void el.offsetWidth; // reinicia la animación
    el.classList.add('aplicado');
  }

  btnApply.addEventListener('click', () => apply());
  btnDiscard.addEventListener('click', () => {
    pending.clear();
    rejected.clear();
    refresh();
  });

  root.querySelector('#btnReleer')!.addEventListener('click', async () => {
    const ack = await gw.command('get');
    if (!ack.ok) log(`get: ${ack.err}`, true);
  });
  root.querySelector('#btnFabrica')!.addEventListener('click', async () => {
    const ok = await confirmDialog({
      title: 'Valores de fábrica',
      body: '<p>El robot vuelve a los valores de <span class="mono">config.h</span> ahora mismo, con el control activo.</p><p>No se borra lo guardado en el robot: al reiniciarlo vuelven los parámetros guardados, salvo que después uses <b>Guardar en el robot</b>.</p>',
      ok: 'Restaurar',
    });
    if (!ok) return;
    pending.clear();
    const ack = await gw.command('defaults');
    if (!ack.ok) log(`defaults: ${ack.err}`, true);
  });
  root.querySelector('#btnGuardarNvs')!.addEventListener('click', async () => {
    const ok = await confirmDialog({
      title: 'Guardar en el robot',
      body: '<p>Los parámetros actuales quedan en la memoria del ESP32 y se cargan en cada arranque, por encima de <span class="mono">config.h</span>.</p><p><b>El control se pausa unos milisegundos y los motores se apagan:</b> sujeta el robot. Se reactiva al ponerlo vertical.</p>',
      ok: 'Guardar',
      danger: true,
    });
    if (!ok) return;
    const ack = await gw.command('save');
    log(ack.ok ? 'parámetros guardados en el robot' : `save: ${ack.err}`, !ack.ok);
  });

  // ------------------------------------------------------------ presentación
  function refresh() {
    const has = schema.length > 0;
    empty.hidden = has;
    foot.hidden = !has;
    const on = connected();
    const st = structure();
    const merged = { ...robot, ...Object.fromEntries(pending) };
    const errs = validate(schema, merged);

    for (const [key, c] of controls) {
      const v = pending.has(key) ? pending.get(key) : robot[key];
      if (v !== undefined) c.set(v);
      c.enable(on && !busy);
      const ui = uiFor(c.d);
      c.root.classList.toggle('pendiente', pending.has(key));
      c.root.classList.toggle('modificado', robot[key] !== undefined && !sameValue(robot[key], c.d.def));
      c.root.classList.toggle('sin-uso', !!ui.only && !!st && ui.only !== st);
      c.root.querySelector('.only')!.textContent = ui.only && st && ui.only !== st ? ` Sin efecto con ${st}.` : '';
      const err = errs[key] ?? rejected.get(key);
      c.root.classList.toggle('error', !!err);
      c.root.querySelector('.err-msg')!.textContent = err ? ` ${err}.` : '';
      (c.root.querySelector('.reset') as HTMLButtonElement).disabled = !on || sameValue(v, c.d.def);
    }
    groupsEl.querySelectorAll('details.grupo').forEach((det) => {
      const n = det.querySelectorAll('.ctl.pendiente').length;
      det.querySelector('.n')!.textContent = n ? `${n} sin aplicar` : '';
    });
    foot.querySelectorAll('button').forEach((b) => ((b as HTMLButtonElement).disabled = !on || busy));
    renderBar();
  }

  function renderBar() {
    const n = pending.size;
    // Sólo en modo por lotes, y visible todo el tiempo que dure: no aparece y desaparece
    bar.hidden = live || schema.length === 0;
    btnApply.disabled = busy || !connected() || n === 0;
    btnDiscard.disabled = n === 0;
    btnApply.textContent = busy ? 'Enviando…' : n ? `Aplicar ${n}` : 'Aplicar';
    msg.textContent = n ? `${n} ${n === 1 ? 'cambio' : 'cambios'} sin aplicar` : 'Sin cambios pendientes';
  }

  // ------------------------------------------------------------ datos del gateway
  gw.on('schema', (s) => {
    const changed = JSON.stringify(s ?? []) !== JSON.stringify(schema);
    schema = s ?? [];
    if (changed) {
      pending.clear();
      build();
    }
  });
  gw.on('params', (p) => {
    robot = p ?? {};
    for (const [k, v] of pending) if (sameValue(robot[k], v)) pending.delete(k);
    refresh();
  });
  gw.on('status', refresh);
  gw.on('link', refresh);

  return {
    /** Valores que se verían en el robot si se aplicaran los pendientes. */
    current: () => ({ ...robot }),
  };
}
