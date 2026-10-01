#pragma once

// Standalone hardware debug page for the main controller, served at GET
// /debug. Polls /api/debug for live readback and posts to /api/debug/test
// to trigger actuators. Kept as a single self-contained string (no external
// assets) so it works straight off the AP with nothing else running.
// See spec/components.md for the component list this mirrors.

inline const char kDebugPageHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="nl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Chronolab Debug</title>
<style>
  :root { color-scheme: dark; }
  body { font-family: -apple-system, Segoe UI, sans-serif; background:#12151a; color:#e8eaed; margin:0; padding:16px; }
  h1 { font-size:18px; margin:0 0 4px; }
  p.sub { color:#9aa0a6; margin:0 0 16px; font-size:13px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fill,minmax(260px,1fr)); gap:12px; }
  .card { background:#1c2027; border:1px solid #2a2f38; border-radius:10px; padding:12px 14px; }
  .card h2 { font-size:14px; margin:0 0 8px; display:flex; justify-content:space-between; align-items:center; }
  .status { font-size:11px; padding:2px 7px; border-radius:10px; font-weight:600; }
  .st-connected { background:#123d24; color:#5fd88a; }
  .st-not_connected { background:#3d2f12; color:#e0b256; }
  .st-removed { background:#3d1414; color:#e08080; }
  .row { display:flex; justify-content:space-between; font-size:13px; padding:3px 0; border-bottom:1px dashed #2a2f38; }
  .row:last-child { border-bottom:none; }
  .row span:first-child { color:#9aa0a6; }
  .val { font-family:ui-monospace, Consolas, monospace; }
  .btns { margin-top:8px; display:flex; flex-wrap:wrap; gap:6px; }
  button { background:#2a2f38; color:#e8eaed; border:1px solid #3a4150; border-radius:6px; padding:5px 10px; font-size:12px; cursor:pointer; }
  button:active { background:#3a4150; }
  .note { color:#6d7378; font-size:11px; margin-top:6px; }
  .on { color:#5fd88a; font-weight:600; }
  .off { color:#6d7378; }
</style>
</head>
<body>
<h1>Chronolab-X13 &mdash; Hardware Debug</h1>
<p class="sub">Live status ververst elke 700ms. Testknoppen sturen echte hardware aan &mdash; kijk/voel/luister naar het fysieke resultaat.</p>
<div class="grid" id="grid"></div>

<script>
const $ = (id) => document.getElementById(id);

function statusBadge(s) {
  return `<span class="status st-${s}">${s.replace('_',' ')}</span>`;
}

function card(id, title, status, bodyHtml) {
  return `<div class="card" id="card-${id}"><h2>${title} ${statusBadge(status)}</h2>${bodyHtml}</div>`;
}

function toast(msg, ok) {
  let t = $('toast');
  if (!t) {
    t = document.createElement('div');
    t.id = 'toast';
    t.style.cssText = 'position:fixed;bottom:16px;left:50%;transform:translateX(-50%);' +
      'padding:8px 16px;border-radius:8px;font-size:13px;z-index:9;transition:opacity .2s;';
    document.body.appendChild(t);
  }
  t.textContent = msg;
  t.style.background = ok ? '#123d24' : '#3d1414';
  t.style.color = ok ? '#5fd88a' : '#e08080';
  t.style.opacity = '1';
  clearTimeout(t._hideTimer);
  t._hideTimer = setTimeout(() => { t.style.opacity = '0'; }, 1500);
}

async function post(component, action, extra) {
  try {
    const res = await fetch('/api/debug/test', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(Object.assign({component, action}, extra || {}))
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      toast(`Fout: ${err.error || res.status}`, false);
      return;
    }
    toast(`${component}.${action} verzonden`, true);
    refresh();  // don't wait for the next 700ms tick to show the effect
  } catch (e) {
    toast('Kon server niet bereiken', false);
    console.error(e);
  }
}

function encoderCard(i, e, noBlue) {
  const btns = [
    `<button onclick="post('encoderLed','set',{index:${i},r:255,g:0,b:0})">Rood</button>`,
    `<button onclick="post('encoderLed','set',{index:${i},r:0,g:255,b:0})">Groen</button>`,
    noBlue ? '' : `<button onclick="post('encoderLed','set',{index:${i},r:0,g:0,b:255})">Blauw</button>`,
    `<button onclick="post('encoderLed','set',{index:${i},r:0,g:0,b:0})">Uit</button>`,
  ].join('');
  return card('enc' + i, `Encoder ${i + 1}` + (noBlue ? ' (geen blauw)' : ' (RGB)'), 'connected', `
    <div class="row"><span>Waarde (raw)</span><span class="val">${e.value}</span></div>
    <div class="row"><span>Knop</span><span class="val ${e.buttonPressed ? 'on' : 'off'}">${e.buttonPressed ? 'INGEDRUKT' : 'los'}</span></div>
    <div class="btns">${btns}</div>
  `);
}

async function refresh() {
  let d;
  try {
    d = await (await fetch('/api/debug')).json();
  } catch (e) {
    $('grid').innerHTML = '<p style="color:#e08080">Kon /api/debug niet bereiken.</p>';
    return;
  }

  let html = '';

  const buttonRows = [...Array(8).keys()].map((i) => {
    const pressed = (d.tm1638.buttonMask & (1 << i)) !== 0;
    return `<div class="row"><span>S${i + 1}</span><span class="val ${pressed ? 'on' : 'off'}">${pressed ? 'INGEDRUKT' : 'los'}</span></div>`;
  }).join('');
  html += card('tm1638', 'TM1638 LED&amp;KEY', 'connected', `
    ${buttonRows}
    <div class="row"><span>Digit/LED-testpatroon</span><span class="val ${d.tm1638.displayTestActive ? 'on' : 'off'}">${d.tm1638.displayTestActive ? 'AAN' : 'uit'}</span></div>
    <div class="btns">
      <button onclick="post('tm1638Display','on')">Testpatroon AAN</button>
      <button onclick="post('tm1638Display','off')">Terug naar normaal</button>
    </div>
    <div class="note">AAN toont "12345678" + alle 8 LED's; controleer elk cijfer en elke LED. Bit n van de knoppen-mask = fysieke knop S(n+1) (zie spec/puzzlebox_hw.md &sect;4.4).</div>
  `);

  for (let i = 0; i < d.encoders.length; i++) {
    html += encoderCard(i, d.encoders[i], i < 2);
  }

  html += card('vibration', 'Trilmotor', 'connected', `
    <div class="row"><span>Actief</span><span class="val ${d.vibration.active ? 'on' : 'off'}">${d.vibration.active ? 'JA' : 'nee'}</span></div>
    <div class="btns"><button onclick="post('vibration','pulse')">Puls 300ms</button></div>
  `);

  html += card('servo', 'Servo-slot', 'not_connected', `
    <div class="row"><span>Stand</span><span class="val">${d.servo.isOpen ? 'OPEN' : 'DICHT'}</span></div>
    <div class="btns">
      <button onclick="post('servo','open')">Open</button>
      <button onclick="post('servo','close')">Dicht</button>
    </div>
    <div class="note">Geen terugkoppeling &mdash; controleer visueel. Wordt overschreven bij de volgende spelstatus-wissel.</div>
  `);

  html += card('rfid', 'RC522 RFID', d.rfid.ready ? 'connected' : 'not_connected', `
    <div class="row"><span>Klaar</span><span class="val ${d.rfid.ready ? 'on' : 'off'}">${d.rfid.ready ? 'ja' : 'nee'}</span></div>
    <div class="row"><span>Laatste tag</span><span class="val">${d.rfid.lastTag}</span></div>
    <div class="note">Trigger door fysiek een tag te scannen.</div>
  `);

  html += card('rtc', 'RTC (DS3231/DS1307)', d.rtc.ready ? 'connected' : 'not_connected', `
    <div class="row"><span>Klaar</span><span class="val ${d.rtc.ready ? 'on' : 'off'}">${d.rtc.ready ? 'ja' : 'nee'}</span></div>
    <div class="row"><span>Tijd</span><span class="val">${d.rtc.now}</span></div>
  `);

  html += card('color', 'TCS3200 Kleursensor', d.colorSensor.hasSignal ? 'connected' : 'not_connected', `
    <div class="row"><span>Signaal</span><span class="val ${d.colorSensor.hasSignal ? 'on' : 'off'}">${d.colorSensor.hasSignal ? 'ja' : 'nee'}</span></div>
    <div class="row"><span>Kleur</span><span class="val">${d.colorSensor.label}</span></div>
    <div class="row"><span>R/G/B Hz</span><span class="val">${d.colorSensor.r} / ${d.colorSensor.g} / ${d.colorSensor.b}</span></div>
    <div class="note">Houd een object voor de sensor.</div>
  `);

  const imu = d.imu;
  html += card('imu', 'GY-91 IMU (via Trinket M0)', imu.online ? 'connected' : 'not_connected', `
    <div class="row"><span>Verbinding</span><span class="val ${imu.online ? 'on' : 'off'}">${imu.online ? 'ontvangt' : 'geen data'}</span></div>
    ${imu.error ? `<div class="row"><span>Sensorfout</span><span class="val" style="color:#e08080">${imu.error}</span></div>` : ''}
    <div class="row"><span>Kanteling roll / pitch</span><span class="val">${imu.roll.toFixed(1)}&deg; / ${imu.pitch.toFixed(1)}&deg;</span></div>
    <div class="row"><span>Versnelling (g)</span><span class="val">${(imu.ax/1000).toFixed(2)} / ${(imu.ay/1000).toFixed(2)} / ${(imu.az/1000).toFixed(2)}</span></div>
    <div class="row"><span>Gyro (&deg;/s)</span><span class="val">${(imu.gx/10).toFixed(1)} / ${(imu.gy/10).toFixed(1)} / ${(imu.gz/10).toFixed(1)}</span></div>
    <div class="row"><span>Kompas (&micro;T)</span><span class="val">${(imu.mx/10).toFixed(1)} / ${(imu.my/10).toFixed(1)} / ${(imu.mz/10).toFixed(1)}</span></div>
    <div class="row"><span>Luchtdruk</span><span class="val">${(imu.pressurePa/100).toFixed(2)} hPa</span></div>
    <div class="row"><span>Temperatuur</span><span class="val">${(imu.tempCenti/100).toFixed(2)} &deg;C</span></div>
    <div class="row"><span>Regels ok / fout / gemist</span><span class="val">${imu.linesOk} / ${imu.linesBad} / ${imu.dropped}</span></div>
    <div class="note">Trinket pin 4 (TX) &rarr; GPIO27, plus gedeelde GND. Versnelling nog niet gekalibreerd (in rust ~1,7 g i.p.v. 1 g).</div>
  `);

  html += card('hidden', 'Verborgen sensoren', (d.hidden.sensor1 || d.hidden.sensor2) ? 'connected' : 'not_connected', `
    <div class="row"><span>Sensor 1 (GPIO35)</span><span class="val ${d.hidden.sensor1 ? 'on' : 'off'}">${d.hidden.sensor1 ? 'actief' : 'rust'}</span></div>
    <div class="row"><span>Sensor 2 (GPIO14)</span><span class="val ${d.hidden.sensor2 ? 'on' : 'off'}">${d.hidden.sensor2 ? 'actief' : 'rust'}</span></div>
  `);

  html += card('matrix', 'WS2812 Matrices', 'removed', `
    <div class="btns"><button onclick="post('matrix','test')">Testpatroon</button></div>
    <div class="note">Hardware fysiek verwijderd &mdash; knop stuurt nog wel GPIO13 aan, verwacht geen effect tenzij je een matrix aansluit.</div>
  `);

  html += card('audio', 'Audio cue bus', 'connected', `
    <div class="row"><span>Voice 0 Hz</span><span class="val">${d.audio.voice0Hz.toFixed(0)}</span></div>
    <div class="row"><span>Laatste cue-seq</span><span class="val">${d.audio.cueSeq}</span></div>
    <div class="btns">
      <button onclick="post('audio','tone')">Test toon (440Hz)</button>
      <button onclick="post('audio','cue')">Test cue</button>
    </div>
    <div class="note">De main controller heeft geen speaker &mdash; hoorbaar via het display-bord (s3_display), alleen als dat online is.</div>
  `);

  html += card('display', 'Main Display (s3_display)', 'connected', `
    <div class="row"><span>Testpatroon</span><span class="val ${d.display.testPatternActive ? 'on' : 'off'}">${d.display.testPatternActive ? 'AAN' : 'uit'}</span></div>
    <div class="btns">
      <button onclick="post('display','on')">Testpatroon AAN</button>
      <button onclick="post('display','off')">Terug naar normaal</button>
    </div>
    <div class="note">AAN toont rood/groen/blauw/wit-balken + live aanraakco&ouml;rdinaten op het 800x480-scherm. Alleen zichtbaar als het display-bord online is en /api/game pollt (duurt tot ~1s na de knop).</div>
  `);

  $('grid').innerHTML = html;
}

refresh();
setInterval(refresh, 700);
</script>
</body>
</html>
)HTML";
