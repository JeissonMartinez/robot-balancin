/** Cifras de cabecera con la última trama. Se refrescan a 5 Hz para que se puedan leer. */
import type { Gateway } from '../core/gateway';
import type { Frame, MotFrame, RobotState } from '../core/protocol';

const STATE: Record<RobotState, [string, string]> = {
  ACTIVE: ['Controlando', 'ok'],
  IDLE: ['Motores off', 'info'],
  ESTOP: ['Parada', 'mal'],
  BENCH: ['Banco', 'warn'],
};

const fmt = (v: number | undefined, d: number) => (v === undefined || v === null || !isFinite(v) ? '—' : v.toFixed(d));

export function setupTiles(root: HTMLElement, gw: Gateway) {
  root.innerHTML = `
    <div class="tile"><span class="lbl">Robot</span><span class="val" id="tEstado">—</span><span class="foot" id="tEstadoF">sin datos</span></div>
    <div class="tile"><span class="lbl">Ángulo θ</span><span class="val mono" id="tAng">—</span><span class="foot mono" id="tRef">ref —</span></div>
    <div class="tile"><span class="lbl">PWM</span><span class="val mono" id="tPwm">—</span><span class="foot mono" id="tPwmM">motor —</span></div>
    <div class="tile"><span class="lbl">Kp</span><span class="val mono" id="tKp">—</span><span class="foot mono" id="tW">ω —</span></div>
    <div class="tile"><span class="lbl">Ruedas [RPM]</span><span class="val mono dual" id="tRpm">—</span><span class="foot">izquierda · derecha</span></div>
    <div class="tile"><span class="lbl">Ciclo</span><span class="val mono" id="tDt">—</span><span class="foot mono" id="tRate">— Hz</span></div>`;

  const $ = (id: string) => root.querySelector<HTMLElement>('#' + id)!;
  let last: Frame | null = null;
  let lastMot: MotFrame | null = null; // banco de motores: manda sobre la telemetría del control

  gw.on('frames', (fs) => {
    last = fs[fs.length - 1] ?? last;
    if (fs.length) lastMot = null;
  });
  gw.on('mot', (fs) => (lastMot = fs[fs.length - 1] ?? lastMot));
  gw.on('status', (s) => {
    if (s.state !== 'connected') last = lastMot = null;
    $('tRate').textContent = s.state !== 'connected' ? '— Hz' : `${s.rate.toFixed(1)} Hz` + (s.gaps ? ` · ${s.gaps} perdidas` : '');
  });

  setInterval(() => {
    if (lastMot) {
      const m = lastMot;
      const [label, cls] = STATE.BENCH;
      $('tEstado').innerHTML = `<span class="pill ${m.es ? 'mal' : cls} tile-pill">${m.es ? 'Banco · parada' : label}</span>`;
      $('tEstadoF').textContent = m.cs === 'running' ? `ensayo ${m.ci}: ${m.ck}/${m.cn}` : `trama ${m.seq}`;
      $('tAng').innerHTML = '—';
      $('tRef').textContent = 'control en pausa';
      $('tPwm').innerHTML = `${m.pwmL}<small> · </small>${m.pwmR}`;
      $('tPwmM').textContent = 'izquierda · derecha';
      $('tKp').textContent = '—';
      $('tW').textContent = 'ω —';
      $('tRpm').innerHTML = `${fmt(m.rpmL, 1)}<small> · </small>${fmt(m.rpmR, 1)}`;
      $('tDt').innerHTML = '1<small> ms</small>';
      return;
    }
    if (!last) {
      $('tEstado').innerHTML = '—';
      $('tEstadoF').textContent = 'sin datos';
      return;
    }
    const [label, cls] = STATE[last.st] ?? [last.st, ''];
    $('tEstado').innerHTML = `<span class="pill ${cls} tile-pill">${label}</span>`;
    $('tEstadoF').textContent = `trama ${last.seq}`;
    $('tAng').innerHTML = `${fmt(last.ang, 2)}<small>°</small>`;
    $('tRef').textContent = `ref ${fmt(last.ref, 2)}°`;
    $('tPwm').textContent = fmt(last.pwm, 1);
    $('tPwmM').textContent = `motor ${fmt(last.pwmM, 1)}`;
    $('tKp').textContent = fmt(last.kp, 2);
    $('tW').textContent = `ω ${fmt(last.w, 1)} °/s`;
    $('tRpm').innerHTML = `${fmt(last.rpmL, 1)}<small> · </small>${fmt(last.rpmR, 1)}`;
    $('tDt').innerHTML = `${fmt(last.dt * 1000, 1)}<small> ms</small>`;
  }, 200);
}
