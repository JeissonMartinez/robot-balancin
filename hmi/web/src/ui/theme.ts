/**
 * Tema: sistema → claro → oscuro → sistema. Se guarda en localStorage (preferencia
 * del navegador, no de la base de datos). Emite 'themechange' en document para
 * que los gráficos en canvas relean los colores.
 */
type Mode = 'auto' | 'light' | 'dark';
const LABEL: Record<Mode, string> = { auto: 'Tema: sistema', light: 'Tema: claro', dark: 'Tema: oscuro' };
const NEXT: Record<Mode, Mode> = { auto: 'light', light: 'dark', dark: 'auto' };

function read(): Mode {
  try {
    const t = localStorage.getItem('tema');
    return t === 'light' || t === 'dark' ? t : 'auto';
  } catch {
    return 'auto';
  }
}

function apply(mode: Mode, btn: HTMLButtonElement) {
  const root = document.documentElement;
  if (mode === 'auto') delete root.dataset.theme;
  else root.dataset.theme = mode;
  try {
    if (mode === 'auto') localStorage.removeItem('tema');
    else localStorage.setItem('tema', mode);
  } catch {
    /* sin almacenamiento: el tema dura hasta recargar */
  }
  btn.textContent = LABEL[mode];
  document.dispatchEvent(new Event('themechange'));
}

export function setupTheme(btn: HTMLButtonElement) {
  let mode = read();
  btn.textContent = LABEL[mode];
  btn.addEventListener('click', () => apply((mode = NEXT[mode]), btn));
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    if (mode === 'auto') document.dispatchEvent(new Event('themechange'));
  });
}

/** Valor resuelto de un token CSS (p. ej. "--rojo") en el tema actual. */
export function cssVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}
