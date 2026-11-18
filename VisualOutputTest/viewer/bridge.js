/* Optional local USB transport supplied only by bridge.py. */
globalThis.localSerial = {
  async requestPort() {
    let controller, active = true;
    const abort = new AbortController();
    return {
      readable: new ReadableStream({
        start(c) { controller = c; },
        cancel() { active = false; abort.abort(); }
      }),
      writable: new WritableStream({
        async write() {
          try {
            const response = await fetch('/capture', {
              method: 'POST', headers: {'Content-Type': 'application/json'},
              body: '{}', signal: abort.signal
            });
            if (!response.ok) throw new Error(await response.text());
            const frame = await response.json();
            if (active) controller.enqueue(new TextEncoder().encode(JSON.stringify(frame) + '\n'));
            // No unsolicited 'ready' reaches the browser through the bridge, so
            // after a device error tell the viewer to ask again shortly.
            if (frame.kind === 'error') setTimeout(() => {
              if (active) controller.enqueue(new TextEncoder().encode('{"kind":"ready"}\n'));
            }, 3000);
          } catch (error) {
            if (active) controller.enqueue(new TextEncoder().encode(JSON.stringify({kind:'error',message:'Local USB: ' + error.message}) + '\n'));
          }
        }
      }),
      async open() {},
      async close() { active = false; abort.abort(); }
    };
  }
};
