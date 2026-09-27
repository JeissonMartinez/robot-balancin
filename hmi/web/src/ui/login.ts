/**
 * Clave de acceso (opcional). Si el gateway se arrancó con --clave, antes de conectar se
 * muestra este formulario; al acertar, el gateway deja una cookie que el navegador recuerda
 * 30 días. Sin clave configurada, no aparece nada.
 */
import { api } from '../core/gateway';

export async function ensureAccess(): Promise<{ required: boolean }> {
  let status: { required: boolean; ok: boolean };
  try {
    status = await api.auth();
  } catch {
    return { required: false }; // sin gateway: la reconexión lo resolverá
  }
  if (!status.required || status.ok) return { required: status.required };

  return new Promise((resolve) => {
    const box = document.createElement('div');
    box.className = 'login';
    box.innerHTML = `
      <form class="login-card card" autocomplete="on">
        <img src="/logos/mecatronica.png" alt="" class="login-logo">
        <h2>Laboratorio del balancín</h2>
        <p class="muted">Este laboratorio pide una clave de acceso.</p>
        <input type="password" name="clave" autocomplete="current-password" placeholder="Clave" required>
        <div class="err" hidden></div>
        <button class="btn primary" type="submit">Entrar</button>
      </form>`;
    document.body.append(box);
    const form = box.querySelector('form')!;
    const input = box.querySelector('input')!;
    const err = box.querySelector<HTMLElement>('.err')!;
    input.focus();
    form.addEventListener('submit', async (e) => {
      e.preventDefault();
      err.hidden = true;
      try {
        await api.login(input.value);
        box.remove();
        resolve({ required: true });
      } catch (x) {
        err.textContent = (x as Error).message === 'clave incorrecta' ? 'Clave incorrecta.' : (x as Error).message;
        err.hidden = false;
        input.select();
      }
    });
  });
}
