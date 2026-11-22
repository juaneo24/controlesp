// Página web que sirve el ESP32. Se guarda en la memoria flash (PROGMEM).
// Pide /api/estado cada segundo y envía cambios a /api/salida.
#pragma once
#include <pgmspace.h>

const char PAGINA_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Control ESP32</title>
<style>
  :root { --bg:#f4f5f7; --card:#fff; --txt:#1d2433; --sub:#6b7280; --on:#16a34a; --off:#d1d5db; --acc:#2563eb; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#0f1115; --card:#1a1d24; --txt:#e5e7eb; --sub:#9ca3af; --off:#374151; }
  }
  * { box-sizing: border-box; }
  body { margin:0; font-family: system-ui, sans-serif; background:var(--bg); color:var(--txt); }
  header { padding:16px; display:flex; justify-content:space-between; align-items:center; }
  h1 { font-size:1.3rem; margin:0; }
  #estado { font-size:.85rem; color:var(--sub); }
  main { padding:0 16px 24px; max-width:720px; margin:auto; }
  h2 { font-size:1rem; color:var(--sub); margin:20px 0 8px; }
  .grid { display:grid; gap:12px; grid-template-columns:repeat(auto-fill,minmax(200px,1fr)); }
  .card { background:var(--card); border-radius:12px; padding:14px; box-shadow:0 1px 3px rgba(0,0,0,.08); }
  .nombre { font-weight:600; }
  .gpio { font-size:.8rem; color:var(--sub); }
  .fila { display:flex; justify-content:space-between; align-items:center; margin-top:10px; }
  button.toggle { border:0; border-radius:999px; width:64px; height:32px; cursor:pointer;
                  background:var(--off); color:#fff; font-weight:600; transition:background .15s; }
  button.toggle.on { background:var(--on); }
  input[type=range] { width:100%; accent-color:var(--acc); }
  .valor { font-size:1.6rem; font-weight:700; margin-top:6px; }
  .barra { height:6px; background:var(--off); border-radius:3px; margin-top:8px; overflow:hidden; }
  .barra > div { height:100%; background:var(--acc); }
</style>
</head>
<body>
<header><h1>Control ESP32</h1><span id="estado">conectando…</span></header>
<main>
  <h2>Salidas</h2>
  <div class="grid" id="salidas"></div>
  <h2>Entradas</h2>
  <div class="grid" id="entradas"></div>
</main>
<script>
const $ = (id) => document.getElementById(id);
let construido = false;
let arrastrando = false;

async function cambiarSalida(id, valor) {
  const cuerpo = new URLSearchParams({ id, valor });
  await fetch('/api/salida', { method: 'POST', body: cuerpo });
  actualizar();
}

function construir(d) {
  $('salidas').innerHTML = d.salidas.map(s => `
    <div class="card">
      <div class="nombre">${s.nombre}</div><div class="gpio">GPIO ${s.gpio}${s.pwm ? ' · PWM' : ''}</div>
      <div class="fila">` + (s.pwm
        ? `<input type="range" min="0" max="255" id="s${s.id}"
             onpointerdown="arrastrando=true" onpointerup="arrastrando=false"
             onchange="cambiarSalida(${s.id}, this.value)">`
        : `<span class="gpio">Encender / apagar</span>
           <button class="toggle" id="s${s.id}" onclick="cambiarSalida(${s.id}, this.classList.contains('on') ? 0 : 1)"></button>`) +
      `</div></div>`).join('');
  $('entradas').innerHTML = d.entradas.map(e => `
    <div class="card">
      <div class="nombre">${e.nombre}</div><div class="gpio">GPIO ${e.gpio} · ${e.tipo}</div>
      <div class="valor" id="e${e.id}">–</div>
      ${e.tipo === 'analogica' ? `<div class="barra"><div id="b${e.id}"></div></div>` : ''}
    </div>`).join('');
  construido = true;
}

function pintar(d) {
  for (const s of d.salidas) {
    const el = $('s' + s.id);
    if (s.pwm) { if (!arrastrando) el.value = s.valor; }
    else {
      el.classList.toggle('on', s.valor === 1);
      el.textContent = s.valor ? 'ON' : 'OFF';
    }
  }
  for (const e of d.entradas) {
    $('e' + e.id).textContent = e.tipo === 'digital' ? (e.valor ? 'ALTO (1)' : 'BAJO (0)') : e.valor;
    if (e.tipo === 'analogica') $('b' + e.id).style.width = (e.valor / 4095 * 100) + '%';
  }
  $('estado').textContent = `${d.ip} · WiFi ${d.rssi} dBm · ${d.uptime_s}s`;
}

async function actualizar() {
  try {
    const r = await fetch('/api/estado');
    const d = await r.json();
    if (!construido) construir(d);
    pintar(d);
  } catch (err) {
    $('estado').textContent = 'sin conexión con el ESP32';
  }
}

actualizar();
setInterval(actualizar, 1000);
</script>
</body>
</html>)rawliteral";
