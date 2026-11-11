#pragma once
#include <Arduino.h>

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 light</title>
<style>
  :root { --bg:#f2f4f3; --panel:#fff; --ink:#1b2421; --line:#cdd5d1; --on:#1f8a5b; --off:#8a949f; --err:#b3392f; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#141a18; --panel:#1d2522; --ink:#e8eeeb; --line:#324039; --on:#3fcf8e; --off:#66727c; --err:#ff7a6e; }
  }
  * { box-sizing: border-box; }
  body { margin:0; min-height:100vh; display:grid; place-items:center; background:var(--bg);
         color:var(--ink); font:16px/1.5 "Segoe UI", system-ui, sans-serif; padding:24px; }
  main { width:100%; max-width:380px; background:var(--panel); border:1px solid var(--line);
         border-radius:14px; padding:28px; text-align:center; }
  h1 { font-size:1.25rem; margin:0 0 8px; }
  .lamp { width:120px; height:120px; border-radius:50%; margin:24px auto 16px; background:var(--off);
          transition:background .2s, box-shadow .2s; }
  .lamp.on { background:var(--on); box-shadow:0 0 40px var(--on); }
  /* 1 Spotlight: small, tight, bright white-green beam */
.lamp[data-level="1"] { background:radial-gradient(circle, #fff 0 18%, var(--on) 19%);
                        box-shadow:0 0 14px 2px var(--on); }

/* 2 Floodlight: same lamp, wide soft glow */
.lamp[data-level="2"] { background:var(--on); box-shadow:0 0 90px 30px var(--on); }

/* 3 Ultra Bright: near-white, biggest glow */
.lamp[data-level="3"] { background:#fff; box-shadow:0 0 110px 45px #fff, 0 0 40px var(--on); }

/* 4 Red Light: steady red */
.lamp[data-level="4"] { background:#e53935; box-shadow:0 0 50px 8px #e53935; }

/* 5 Flashing Red: red, blinking */
.lamp[data-level="5"] { background:#e53935; box-shadow:0 0 50px 8px #e53935;
                        animation:blink 0.6s steps(1, end) infinite; }
@keyframes blink { 50% { background:var(--off); box-shadow:none; } }
  #state { font-size:1.1rem; font-weight:600; margin-bottom:20px; }
  button { width:100%; padding:12px; font:inherit; font-weight:600; cursor:pointer; color:#fff;
           background:var(--on); border:0; border-radius:8px; }
  @media (prefers-color-scheme: dark) { button { color:#0b1411; } }
  button:focus-visible { outline:3px solid var(--on); outline-offset:2px; }
  button:disabled { opacity:.6; }
  #msg { margin:14px 0 0; min-height:1.5em; font-size:.875rem; color:var(--err); }
</style>
</head>
<body>
<main>
  <h1>ESP32 light</h1>
  <div id="lamp" class="lamp"></div>
  <div id="state" aria-live="polite">Unknown</div>
  <button id="btn">Toggle light</button>
  <p id="msg" role="status"></p>
</main>
<script>
  const lamp = document.getElementById("lamp");
  const stateEl = document.getElementById("state");
  const btn = document.getElementById("btn");
  const msg = document.getElementById("msg");

  const LIGHT_STATES = ["Off", "Spotlight", "Floodlight", "Ultra Bright", "Red Light", "Flashing Red Light"];
  function show(text) {
    let level = parseInt(text.trim(), 10);
    if (!(level >= 0 && level < LIGHT_STATES.length)) level = 0;
    lamp.classList.toggle("on", level > 0);
    lamp.dataset.level = level;
    stateEl.textContent = LIGHT_STATES[level];
    msg.textContent = "";
  }
  async function call(path) {
    btn.disabled = true;
    try {
      const r = await fetch(path);
      show(await r.text());
    } catch (e) {
      msg.textContent = "Lost connection to the ESP32.";
    } finally {
      btn.disabled = false;
    }
  }
  btn.addEventListener("click", () => call("/toggle"));
  call("/state");
</script>
</body>
</html>
)rawliteral";