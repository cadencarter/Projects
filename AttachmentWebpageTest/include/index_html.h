#pragma once
#include <Arduino.h>

// Web page for the hub. It asks /status every 2 seconds which attachment is
// connected and shows the matching panel:
//   "light"   -> lamp panel (plugged into USB-C, found by its ID resistor)
//   "gps_env" -> GPS + environment readings (sent by the attachment over WiFi)
//   "none"    -> "No attachment" message
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 attachment</title>
<style>
  :root { --bg:#f2f4f3; --panel:#fff; --ink:#1b2421; --muted:#5d6a64; --line:#cdd5d1; --on:#1f8a5b; --off:#8a949f; --err:#b3392f;
          --good:#1f8a5b; --fair:#b08a00; --poor:#c4621a; --bad:#b3392f; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#141a18; --panel:#1d2522; --ink:#e8eeeb; --muted:#9aa7a1; --line:#324039; --on:#3fcf8e; --off:#66727c; --err:#ff7a6e;
            --good:#3fcf8e; --fair:#e0c04a; --poor:#f08c4a; --bad:#ff7a6e; }
  }
  * { box-sizing: border-box; }
  body { margin:0; min-height:100vh; display:grid; place-items:center; background:var(--bg);
         color:var(--ink); font:16px/1.5 "Segoe UI", system-ui, sans-serif; padding:24px 16px; }
  main { width:100%; max-width:420px; background:var(--panel); border:1px solid var(--line);
         border-radius:14px; padding:28px; text-align:center; }
  h1 { font-size:1.25rem; margin:0 0 4px; }
  .sub { margin:0; color:var(--muted); font-size:.875rem; }
  [hidden] { display:none !important; }

  /* ---- Light attachment ---- */
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

  /* ---- GPS + environment attachment ---- */
  section.data { text-align:left; margin-top:20px; }
  h2 { font-size:.8rem; text-transform:uppercase; letter-spacing:.06em; color:var(--muted);
       margin:20px 0 8px; display:flex; justify-content:space-between; gap:8px; }
  h2 .badge { text-transform:none; letter-spacing:0; font-weight:600; }
  .iaq { border:1px solid var(--line); border-radius:10px; padding:14px 16px; }
  .iaq-top { display:flex; align-items:baseline; justify-content:space-between; gap:8px; }
  .iaq-num { font-size:2rem; font-weight:700; font-variant-numeric:tabular-nums; }
  .iaq-rating { font-weight:600; }
  .bar { height:8px; border-radius:4px; background:var(--line); margin-top:8px; overflow:hidden; }
  .bar span { display:block; height:100%; width:0; background:var(--off); transition:width .3s, background .3s; }
  dl { display:grid; grid-template-columns:1fr 1fr; gap:8px; margin:0; }
  dl div { border:1px solid var(--line); border-radius:8px; padding:8px 10px; min-width:0; }
  dt { font-size:.75rem; color:var(--muted); }
  dd { margin:0; font-weight:600; font-variant-numeric:tabular-nums; overflow-wrap:anywhere; }
  dl div.wide { grid-column:1 / -1; }
  .ok { color:var(--good); } .warn { color:var(--fair); }

  #none p { margin:24px 0 8px; color:var(--muted); }
  #msg { margin:14px 0 0; min-height:1.5em; font-size:.875rem; color:var(--err); }
  footer { margin-top:12px; font-size:.75rem; color:var(--muted); }
</style>
</head>
<body>
<main>
  <h1 id="title">ESP32 attachment</h1>
  <p class="sub" id="sub" aria-live="polite">Checking attachment…</p>

  <!-- Light attachment -->
  <div id="light" hidden>
    <div id="lamp" class="lamp"></div>
    <div id="state" aria-live="polite">Unknown</div>
    <button id="btn">Next light mode</button>
  </div>

  <!-- GPS + environment attachment -->
  <section id="gpsenv" class="data" hidden>
    <h2>Air quality</h2>
    <div class="iaq">
      <div class="iaq-top">
        <span class="iaq-num" id="iaq">–</span>
        <span class="iaq-rating" id="iaqRating">–</span>
      </div>
      <div class="bar"><span id="iaqBar"></span></div>
    </div>

    <h2>Environment</h2>
    <dl>
      <div><dt>Temperature</dt><dd id="temp">–</dd></div>
      <div><dt>Humidity</dt><dd id="hum">–</dd></div>
      <div><dt>Pressure</dt><dd id="pres">–</dd></div>
      <div><dt>Gas resistance</dt><dd id="gas">–</dd></div>
    </dl>

    <h2>GPS <span class="badge" id="fix">–</span></h2>
    <dl>
      <div class="wide"><dt>Position</dt><dd id="pos">–</dd></div>
      <div><dt>Altitude</dt><dd id="alt">–</dd></div>
      <div><dt>Speed</dt><dd id="speed">–</dd></div>
      <div><dt>Heading</dt><dd id="course">–</dd></div>
      <div><dt>Satellites</dt><dd id="sats">–</dd></div>
      <div><dt>Accuracy (HDOP)</dt><dd id="hdop">–</dd></div>
      <div><dt>Time (UTC)</dt><dd id="utc">–</dd></div>
    </dl>
    <footer id="age"></footer>
  </section>

  <!-- Nothing plugged in -->
  <div id="none" hidden>
    <p>Plug the light into the USB-C port, or turn on the GPS &amp; environment attachment.</p>
  </div>

  <p id="msg" role="status"></p>
</main>
<script>
  const $ = (id) => document.getElementById(id);
  const msg = $("msg");

  const ATTACHMENTS = {
    light:   { title: "Light attachment",           panel: "light" },
    gps_env: { title: "GPS & environment attachment", panel: "gpsenv" },
    none:    { title: "No attachment",              panel: "none" },
  };
  let current = null;

  // ---------- Light ----------
  const lamp = $("lamp"), stateEl = $("state"), btn = $("btn");
  const LIGHT_STATES = ["Off", "Spotlight", "Floodlight", "Ultra Bright", "Red Light", "Flashing Red Light"];
  function showLight(text) {
    let level = parseInt(text.trim(), 10);
    if (!(level >= 0 && level < LIGHT_STATES.length)) level = 0;
    lamp.classList.toggle("on", level > 0);
    lamp.dataset.level = level;
    stateEl.textContent = LIGHT_STATES[level];
  }
  async function callLight(path) {
    btn.disabled = true;
    try {
      const r = await fetch(path);
      showLight(await r.text());
      msg.textContent = "";
    } catch (e) {
      msg.textContent = "Lost connection to the ESP32.";
    } finally {
      btn.disabled = false;
    }
  }
  btn.addEventListener("click", () => callLight("/toggle"));

  // ---------- GPS + environment ----------
  const fmt = (v, digits, unit) => (v === null || v === undefined) ? "–" : Number(v).toFixed(digits) + unit;
  const set = (id, text) => { $(id).textContent = text; };

  function iaqColour(iaq) {
    if (iaq <= 100) return "var(--good)";
    if (iaq <= 150) return "var(--fair)";
    if (iaq <= 250) return "var(--poor)";
    return "var(--bad)";
  }
  function compass(deg) {
    const dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
    return dirs[Math.round(deg / 45) % 8];
  }

  function showGpsEnv(d, ageMs) {
    if (!d) {
      set("age", "Waiting for data from the attachment…");
      return;
    }

    // Air quality
    const bar = $("iaqBar");
    if (d.iaq === null) {
      set("iaq", "–");
      set("iaqRating", d.iaq_rating || "–");
      bar.style.width = "0";
    } else {
      set("iaq", Math.round(d.iaq));
      set("iaqRating", d.iaq_rating);
      bar.style.width = Math.min(100, d.iaq / 5) + "%";
      bar.style.background = iaqColour(d.iaq);
    }

    // Environment
    set("temp", fmt(d.temp_c, 1, " °C"));
    set("hum",  fmt(d.humidity_pct, 1, " %"));
    set("pres", fmt(d.pressure_hpa, 1, " hPa"));
    set("gas",  fmt(d.gas_kohm, 1, " kΩ"));

    // GPS
    const fix = $("fix");
    fix.textContent = d.fix ? "Fix" : "Searching…";
    fix.className = "badge " + (d.fix ? "ok" : "warn");
    set("pos", d.fix ? d.lat.toFixed(6) + ", " + d.lon.toFixed(6) : "–");
    set("alt",   fmt(d.alt_m, 1, " m"));
    set("speed", fmt(d.speed_kmh, 1, " km/h"));
    set("course", d.course_deg === null ? "–" : Math.round(d.course_deg) + "° " + compass(d.course_deg));
    set("sats", d.sats === null ? "–" : d.sats);
    set("hdop", fmt(d.hdop, 1, ""));
    set("utc", d.utc ? d.utc.replace("T", " ").replace("Z", "") : "–");

    // Freshness and errors from the attachment
    const secs = Math.round((ageMs || 0) / 1000);
    set("age", secs <= 1 ? "Updated just now" : "Updated " + secs + " s ago");
    if (ageMs > 10000) msg.textContent = "No new data from the attachment for " + secs + " s.";
    else if (d.error) msg.textContent = d.error;
    else msg.textContent = "";
  }

  // ---------- Which attachment is plugged in ----------
  function showAttachment(kind) {
    if (!ATTACHMENTS[kind]) kind = "none";
    if (kind === current) return;
    current = kind;
    const a = ATTACHMENTS[kind];
    set("title", a.title);
    for (const k in ATTACHMENTS) $(ATTACHMENTS[k].panel).hidden = (k !== kind);
    msg.textContent = "";
    if (kind === "light") callLight("/state");
  }

  async function poll() {
    try {
      const s = await (await fetch("/status", { cache: "no-store" })).json();
      showAttachment(s.attachment);
      const r = s.resistance_ohm ? " (" + Math.round(s.resistance_ohm) + " Ω)" : "";
      set("sub", s.attachment === "light"   ? "Connected by USB-C" + r
               : s.attachment === "gps_env" ? "Connected over WiFi"
               : "No attachment connected");
      if (s.attachment === "gps_env") showGpsEnv(s.data, s.age_ms);
    } catch (e) {
      set("sub", "Not connected");
      msg.textContent = "Lost connection to the ESP32.";
    }
  }
  poll();
  setInterval(poll, 2000);
</script>
</body>
</html>
)rawliteral";
