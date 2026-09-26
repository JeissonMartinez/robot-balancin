/**
 * Diálogo de confirmación modal (<dialog> nativo: accesible, cierra con Esc).
 * Se usa antes de toda acción que mueve motores, pausa el control o borra datos.
 */
export interface ConfirmOptions {
  title: string;
  body: string; // HTML
  ok: string;
  danger?: boolean;
}

let dlg: HTMLDialogElement | null = null;

export function confirmDialog(o: ConfirmOptions): Promise<boolean> {
  if (!dlg) {
    dlg = document.createElement('dialog');
    dlg.className = 'dlg';
    document.body.append(dlg);
  }
  const d = dlg;
  d.innerHTML = `
    <form method="dialog">
      <h3></h3>
      <div class="dlg-body">${o.body}</div>
      <div class="dlg-actions">
        <button class="btn" value="cancel" type="submit">Cancelar</button>
        <button class="btn ${o.danger ? 'danger' : 'primary'}" value="ok" type="submit">${o.ok}</button>
      </div>
    </form>`;
  d.querySelector('h3')!.textContent = o.title;
  return new Promise((resolve) => {
    d.addEventListener('close', () => resolve(d.returnValue === 'ok'), { once: true });
    d.returnValue = '';
    d.showModal();
    (d.querySelector('button[value="ok"]') as HTMLButtonElement).focus();
  });
}

export const escapeHtml = (s: string) =>
  s.replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);
