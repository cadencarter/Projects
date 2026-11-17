/* Shared by the browser viewer and dependency-free Node tests. */
(function (root) {
  const MAX_LINE = 100000;
  class Lines {
    constructor(accept, fail) { this.accept = accept; this.fail = fail; this.buffer = ''; this.dropping = false; }
    push(text) {
      for (const c of text) {
        if (c === '\n') {
          if (!this.dropping && this.buffer.trim()) {
            try { this.accept(JSON.parse(this.buffer)); }
            catch (error) { this.fail('Invalid serial JSON: ' + error.message); }
          }
          this.buffer = ''; this.dropping = false;
        } else if (!this.dropping) {
          this.buffer += c;
          if (this.buffer.length > MAX_LINE) {
            this.buffer = ''; this.dropping = true; this.fail('Serial response too large.');
          }
        }
      }
    }
  }
  function boxes(data) {
    return (Array.isArray(data.boxes) ? data.boxes : []).filter(b =>
      Array.isArray(b) && b.length >= 6 && b.slice(0, 6).every(Number.isFinite) && b[2] >= 0 && b[3] >= 0);
  }
  function rectangle(b, imageWidth, imageHeight, coordinateWidth, coordinateHeight) {
    const sx = imageWidth / coordinateWidth, sy = imageHeight / coordinateHeight;
    return [(b[0] - b[2] / 2) * sx, (b[1] - b[3] / 2) * sy, b[2] * sx, b[3] * sy];
  }
  const api = { Lines, boxes, rectangle, MAX_LINE };
  if (typeof module !== 'undefined') module.exports = api;
  else root.VisionProtocol = api;
})(typeof globalThis === 'undefined' ? this : globalThis);
