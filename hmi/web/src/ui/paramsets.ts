/**
 * Juegos de parámetros guardados en la base del PC (tabla param_sets).
 *
 * - Guardar los parámetros actuales del robot con nombre y notas.
 * - Cargar un juego en el robot: antes se muestran los cambios que va a producir.
 * - Comparar: cada juego indica cuántos parámetros difieren del robot.
 * - Exportar / importar en JSON para compartir entre equipos.
 */
import { ApiError, api, type Gateway } from '../core/gateway';
import { diffParams, formatValue, uiFor } from '../core/paramspec';
import type { Params, ParamSet } from '../core/protocol';
import { confirmDialog, escapeHtml } from './dialog';

const EXPORT_FORMAT = 'balancin-params';

export function setupParamSets(root: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  root.innerHTML = `
    <form class="ps-form" autocomplete="off">
      <input type="text" id="psNombre" placeholder="Nombre del juego" maxlength="60" required>
      <input type="text" id="psNotas" placeholder="Notas (opcional)" maxlength="200">
      <div class="row">
        <button class="btn small primary" type="submit" id="psGuardar">Guardar los del robot</button>
        <button class="btn small" type="button" id="psImportar">Importar JSON</button>
        <input type="file" id="psArchivo" accept="application/json,.json" hidden>
      </div>
    </form>
    <div class="ps-list" id="psLista"></div>`;

  const form = root.querySelector<HTMLFormElement>('.ps-form')!;
  const name = root.querySelector<HTMLInputElement>('#psNombre')!;
  const notes = root.querySelector<HTMLInputElement>('#psNotas')!;
  const btnSave = root.querySelector<HTMLButtonElement>('#psGuardar')!;
  const file = root.querySelector<HTMLInputElement>('#psArchivo')!;
  const list = root.querySelector<HTMLElement>('#psLista')!;
  let sets: ParamSet[] = [];
  let lastRender = '';

  const connected = () => gw.linkUp && gw.status?.state === 'connected';

  async function save(body: { name: string; notes?: string; params?: Params }) {
    try {
      await api.saveParamSet(body);
      return true;
    } catch (e) {
      if (e instanceof ApiError && e.status === 409) {
        const ok = await confirmDialog({
          title: 'Reemplazar juego',
          body: `<p>Ya existe «${escapeHtml(body.name)}». ¿Reemplazarlo con estos valores?</p>`,
          ok: 'Reemplazar',
        });
        if (ok) {
          await api.saveParamSet({ ...body, overwrite: true });
          return true;
        }
        return false;
      }
      log(`guardar juego: ${(e as Error).message}`, true);
      return false;
    }
  }

  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const n = name.value.trim();
    if (!n) return;
    if (await save({ name: n, notes: notes.value.trim() || undefined })) {
      name.value = notes.value = '';
      log(`juego «${n}» guardado en el PC`);
    }
  });

  root.querySelector('#psImportar')!.addEventListener('click', () => file.click());
  file.addEventListener('change', async () => {
    const f = file.files?.[0];
    file.value = '';
    if (!f) return;
    try {
      const data = JSON.parse(await f.text());
      // Formato exportado, o un objeto plano {clave: valor}
      const params: Params = data?.format === EXPORT_FORMAT ? data.params : data;
      if (!params || typeof params !== 'object' || Array.isArray(params)) throw new Error('no contiene parámetros');
      const n = (data?.name as string) || f.name.replace(/\.json$/i, '');
      if (await save({ name: n, notes: data?.notes ?? undefined, params })) log(`juego «${n}» importado`);
    } catch (err) {
      log(`importar ${f.name}: ${(err as Error).message}`, true);
    }
  });

  function exportSet(s: ParamSet) {
    const blob = new Blob(
      [JSON.stringify({ format: EXPORT_FORMAT, version: 1, name: s.name, notes: s.notes, fw: s.fw, created_at: s.created_at, params: s.params }, null, 2)],
      { type: 'application/json' },
    );
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${s.name.replace(/[^\w\-áéíóúñÁÉÍÓÚÑ ]+/g, '_')}.json`;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  async function load(s: ParamSet) {
    const schema = gw.schema ?? [];
    const robot = gw.params ?? {};
    const keys = diffParams(schema, robot, s.params);
    if (!keys.length) {
      log(`«${s.name}» ya coincide con el robot`);
      return;
    }
    const rows = keys
      .map((k) => {
        const d = schema.find((x) => x.key === k)!;
        return `<tr><td>${escapeHtml(uiFor(d).label)} <span class="mono muted">${k}</span></td><td class="mono">${escapeHtml(formatValue(d, robot[k]))}</td><td>→</td><td class="mono"><b>${escapeHtml(formatValue(d, s.params[k]))}</b></td></tr>`;
      })
      .join('');
    const ok = await confirmDialog({
      title: `Cargar «${s.name}»`,
      body: `<p>Se cambian ${keys.length} ${keys.length === 1 ? 'parámetro' : 'parámetros'} en el robot, con el control activo:</p><table class="diff">${rows}</table>`,
      ok: 'Cargar en el robot',
    });
    if (!ok) return;
    try {
      const r = await api.applyParamSet(s.id);
      if (!r.ok) log(`cargar «${s.name}»: ${r.err}`, true);
      else log(`juego «${s.name}» cargado${r.ignored.length ? ` (omitidos, el firmware no los conoce: ${r.ignored.join(', ')})` : ''}`);
    } catch (e) {
      log(`cargar «${s.name}»: ${(e as Error).message}`, true);
    }
  }

  async function remove(s: ParamSet) {
    const ok = await confirmDialog({
      title: 'Borrar juego',
      body: `<p>Se borra «${escapeHtml(s.name)}» de la base del PC. No afecta al robot.</p>`,
      ok: 'Borrar',
      danger: true,
    });
    if (!ok) return;
    try {
      await api.deleteParamSet(s.id);
    } catch (e) {
      log(`borrar: ${(e as Error).message}`, true);
    }
  }

  function render() {
    const on = connected();
    btnSave.disabled = !on;
    btnSave.title = on ? '' : 'Conectar el robot para guardar sus parámetros';
    const schema = gw.schema ?? [];
    const robot = gw.params;
    const diffs = sets.map((s) => (robot && schema.length ? diffParams(schema, robot, s.params).length : null));
    // Sólo se reconstruye la lista si cambió algo: si no, un clic podría caer en un botón recién reemplazado
    const signature = JSON.stringify([on, sets.map((s) => [s.id, s.created_at]), diffs]);
    if (signature === lastRender) return;
    lastRender = signature;
    if (!sets.length) {
      list.innerHTML = '<div class="muted ps-vacio">Aún no hay juegos guardados.</div>';
      return;
    }
    list.innerHTML = '';
    sets.forEach((s, i) => {
      const n = diffs[i];
      const badge = n === null ? '' : n === 0 ? '<span class="pill ok">= robot</span>' : `<span class="pill">${n} ${n === 1 ? 'diferencia' : 'diferencias'}</span>`;
      const date = new Date(s.created_at).toLocaleString('es-CO', { dateStyle: 'short', timeStyle: 'short' });
      const item = document.createElement('div');
      item.className = 'ps-item';
      item.innerHTML = `
        <div class="ps-top"><span class="ps-name">${escapeHtml(s.name)}</span>${badge}</div>
        <div class="ps-meta muted">${date}${s.fw ? ` · fw ${escapeHtml(s.fw)}` : ''}${s.notes ? ` · ${escapeHtml(s.notes)}` : ''}</div>
        <div class="row">
          <button class="btn small" data-a="cargar" type="button" ${on && n !== 0 ? '' : 'disabled'}>Cargar</button>
          <button class="btn small" data-a="exportar" type="button">Exportar</button>
          <button class="btn small" data-a="borrar" type="button">Borrar</button>
        </div>`;
      item.querySelector('[data-a="cargar"]')!.addEventListener('click', () => load(s));
      item.querySelector('[data-a="exportar"]')!.addEventListener('click', () => exportSet(s));
      item.querySelector('[data-a="borrar"]')!.addEventListener('click', () => remove(s));
      list.append(item);
    });
  }

  async function reload() {
    try {
      sets = await api.paramSets();
    } catch {
      /* sin gateway: se reintenta al reconectar */
    }
    render();
  }

  gw.on('paramSetsChanged', reload);
  gw.on('params', render);
  gw.on('status', (s) => {
    if (s.state !== 'connecting') render();
  });
  gw.on('link', (up) => (up ? reload() : render()));
}
