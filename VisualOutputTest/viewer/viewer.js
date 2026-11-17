'use strict';
const serialProvider = globalThis.localSerial || navigator.serial;
const el = id => document.getElementById(id);
const canvas = el('camera'), ctx = canvas.getContext('2d');
let port = null, reader = null, writer = null, running = false;
let generation = 0, lastFrameAt = 0, lastImage = null, lastFrame = null, renderQueue = Promise.resolve();
function status(text) { el('status').textContent = text; }
function clearFrame(text) {
  lastImage = null; lastFrame = null;
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  el('caption').textContent = text;
  el('person').textContent = 'Person alert: unknown';
  el('results').textContent = 'No current inference.';
}
function draw() {
  if (!lastImage || !lastFrame) return;
  canvas.width = lastImage.naturalWidth; canvas.height = lastImage.naturalHeight;
  ctx.drawImage(lastImage, 0, 0);
  if (!el('overlay').checked) return;
  const cw = Number(el('cw').value) || canvas.width, ch = Number(el('ch').value) || canvas.height;
  if (cw <= 0 || ch <= 0) return;
  ctx.lineWidth = Math.max(2, canvas.width / 250);
  ctx.font = `${Math.max(12, canvas.width / 35)}px sans-serif`;
  for (const b of VisionProtocol.boxes(lastFrame.data)) {
    const [x, y, w, h] = VisionProtocol.rectangle(b, canvas.width, canvas.height, cw, ch);
    const person = b[5] === lastFrame.person_class && b[4] >= lastFrame.threshold;
    ctx.strokeStyle = person ? '#5aff9d' : '#ffcd69'; ctx.fillStyle = ctx.strokeStyle;
    ctx.strokeRect(x, y, w, h);
    const label = `${b[5] === lastFrame.person_class ? 'person' : 'class ' + b[5]} ${b[4]}%`;
    ctx.fillText(label, Math.max(0, x), Math.max(14, y - 4));
  }
}
async function requestFrame() {
  if (running && writer) await writer.write(new TextEncoder().encode('n'));
}
async function handle(message, token) {
  if (!running || token !== generation) return;
  if (message.kind === 'error') {
    status(message.message); clearFrame('Capture error; reset XIAO and reconnect.'); return;
  }
  if (message.kind !== 'frame' || !message.data) return;
  const data = message.data;
  let img = null, imageError = '';
  if (typeof data.image === 'string' && data.image.length) {
    try {
      img = new Image();
      img.src = 'data:image/jpeg;base64,' + data.image;
      await img.decode();
    } catch (_) { img = null; imageError = 'JPEG could not be decoded.'; }
  } else imageError = 'Module returned results without a camera image.';
  if (!running || token !== generation) return;
  clearFrame(imageError || '');
  lastFrame = message; lastImage = img; lastFrameAt = Date.now();
  const detections = VisionProtocol.boxes(data);
  el('person').textContent = `Person alert (D0): ${message.person ? 'ON' : 'OFF'} · threshold ${message.threshold}%`;
  const lines = detections.map(b => `Class ${b[5]}: ${b[4]}% · center (${b[0]}, ${b[1]}) · ${b[2]} × ${b[3]}`);
  if (Array.isArray(data.classes)) lines.push('Classifications: ' + JSON.stringify(data.classes));
  if (Array.isArray(data.points)) lines.push('Points: ' + JSON.stringify(data.points));
  if (Array.isArray(data.keypoints)) lines.push('Keypoints: ' + JSON.stringify(data.keypoints));
  if (Array.isArray(data.perf)) lines.push('Preprocess / inference / postprocess (ms): ' + data.perf.join(' / '));
  el('results').textContent = lines.join('\n\n') || 'No detections in this capture.';
  if (img) { draw(); el('caption').textContent = `Capture ${message.sequence} · ${img.naturalWidth} × ${img.naturalHeight}`; }
  status(imageError || `Receiving camera frames · capture ${message.sequence}`);
  await requestFrame();
}
async function disconnect() {
  running = false; generation++;
  if (reader) { try { await reader.cancel(); } catch (_) {} }
  // readLoop owns lock/port cleanup, including surprise disconnects.
}
async function readLoop(token) {
  const decoder = new TextDecoder();
  const lines = new VisionProtocol.Lines(message => {
    renderQueue = renderQueue.then(() => handle(message, token)).catch(error => {
      status('Viewer error: ' + error.message); clearFrame('No current image.');
    });
  }, error => { status(error + ' Disconnect, reset XIAO and reconnect.'); clearFrame('Invalid data.'); });
  try {
    while (running && token === generation) {
      const { value, done } = await reader.read();
      if (done) break;
      lines.push(decoder.decode(value, { stream: true }));
    }
  } catch (error) { status('USB connection lost: ' + error.message); }
  finally {
    running = false; generation++;
    if (reader) { reader.releaseLock(); reader = null; }
    await renderQueue;
    if (writer) { writer.releaseLock(); writer = null; }
    if (port) { try { await port.close(); } catch (_) {} port = null; }
    clearFrame('Disconnected. No current camera image.');
    status('Disconnected. Connect XIAO to resume.');
    el('connect').disabled = false; el('disconnect').disabled = true;
  }
}
el('connect').onclick = async () => {
  if (!serialProvider) { status('Use desktop Chrome or Edge at http://localhost:8000.'); return; }
  el('connect').disabled = true;
  try {
    port = await serialProvider.requestPort();
    await port.open({ baudRate: 115200, bufferSize: 131072 });
    reader = port.readable.getReader(); writer = port.writable.getWriter();
    running = true; const token = ++generation; lastFrameAt = Date.now();
    status('Connected. Waiting for the first capture (up to 15 seconds).');
    el('disconnect').disabled = false;
    readLoop(token);
    await requestFrame();
  } catch (error) {
    status('Could not connect: ' + error.message);
    if (running) await disconnect();
    else { if (port) { try { await port.close(); } catch (_) {} port = null; } el('connect').disabled = false; }
  }
};
el('disconnect').onclick = disconnect;
for (const id of ['overlay', 'cw', 'ch']) el(id).onchange = draw;
setInterval(() => {
  if (running && Date.now() - lastFrameAt > 20000) {
    clearFrame('No fresh camera frame for 20 seconds.');
    status('No fresh frames. Check USB/I2C, reset XIAO, then disconnect and reconnect.');
  }
}, 1000);
if (!serialProvider) status('This viewer needs desktop Chrome or Edge with Web Serial. Open it at http://localhost:8000.');
