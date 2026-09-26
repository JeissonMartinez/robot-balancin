/**
 * Comandos del robot que antes se daban con teclas en el monitor serie (c, d).
 * Ambos pausan el control; se piden con confirmación y muestran el resultado que el
 * robot imprime (líneas ">> ...").
 */
import type { Gateway } from '../core/gateway';
import { confirmDialog } from './dialog';

interface CommandDef {
  cmd: string;
  label: string;
  running: string;
  title: string;
  body: string;
  danger?: boolean;
}

const COMMANDS: CommandDef[] = [
  {
    cmd: 'calib',
    label: 'Calibrar MPU',
    running: 'Calibrando…',
    title: 'Calibrar el MPU6050',
    body: '<p>Pon el robot <b>quieto y vertical</b> (es el cero del ángulo). El control se pausa ~1.5 s y los offsets quedan guardados en el robot.</p><p>Equivale al botón de la placa o a la tecla <span class="mono">c</span>.</p>',
  },
  {
    cmd: 'deadband',
    label: 'Prueba de zona muerta',
    running: 'Midiendo…',
    title: 'Prueba de zona muerta',
    body: '<p><b>Los motores van a girar.</b> Sujeta el robot vertical con las ruedas en el suelo y déjalo rodar sin empujarlo.</p><p>El PWM sube poco a poco en cada sentido hasta que la rueda derecha gira de forma sostenida (hasta ~20 s). Cualquier comando, incluida la parada, la aborta.</p><p>Equivale a la tecla <span class="mono">d</span>.</p>',
    danger: true,
  },
];

export function setupCommands(root: HTMLElement, gw: Gateway, log: (m: string, bad?: boolean) => void) {
  root.innerHTML = `
    <div class="cmd-btns">${COMMANDS.map((c) => `<button class="btn" type="button" data-cmd="${c.cmd}">${c.label}</button>`).join('')}</div>
    <div class="cmd-out mono" hidden></div>`;
  const out = root.querySelector<HTMLElement>('.cmd-out')!;
  const buttons = [...root.querySelectorAll<HTMLButtonElement>('[data-cmd]')];
  let running: string | null = null;
  let capture: string[] | null = null;

  // Mientras corre un comando se guardan las líneas de resultado que imprime el robot
  gw.on('event', (ev) => {
    if (capture && ev.kind === 'log' && typeof ev.payload?.msg === 'string' && ev.payload.msg.startsWith('>>')) {
      capture.push(ev.payload.msg.replace(/^>>\s*/, ''));
      out.hidden = false;
      out.textContent = capture.join('\n');
    }
  });

  function render() {
    const on = gw.linkUp && gw.status?.state === 'connected';
    buttons.forEach((b) => {
      const c = COMMANDS.find((x) => x.cmd === b.dataset.cmd)!;
      b.disabled = !on || running !== null;
      b.textContent = running === c.cmd ? c.running : c.label;
    });
  }

  buttons.forEach((b) =>
    b.addEventListener('click', async () => {
      const c = COMMANDS.find((x) => x.cmd === b.dataset.cmd)!;
      if (!(await confirmDialog({ title: c.title, body: c.body, ok: c.label, danger: c.danger }))) return;
      running = c.cmd;
      capture = [];
      out.hidden = false;
      out.textContent = c.running;
      render();
      const ack = await gw.command(c.cmd);
      running = null;
      if (!ack.ok) {
        out.textContent = `${c.label}: ${ack.err}`;
        log(`${c.cmd}: ${ack.err}`, true);
      } else if (!capture.length) {
        out.textContent = `${c.label}: listo.`;
      }
      capture = null;
      render();
    }),
  );

  gw.on('status', render);
  gw.on('link', render);
  render();
}
