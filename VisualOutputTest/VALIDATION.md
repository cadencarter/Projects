# Validation — September 24, 2026

## Completed

- Inspected the signed-in private repository at commit
  `ea4ecf890205c44ebca7891e4c09df9b8702d001`, including its source, PlatformIO
  configuration and library folder (which contains a placeholder README).
- Built the complete ESP32-C3 firmware successfully with PlatformIO 6.2.0,
  Espressif32 platform 6.9.0, Arduino ESP32 2.0.17,
  Seeed_Arduino_SSCMA 1.0.0 and ArduinoJson 6.21.5.
- Build size: 98,428 bytes static RAM (30.0%); 292,722 bytes flash (22.3%).
  This is static build usage, not a measurement of peak runtime heap.
- Six automated tests passed: fragmented/multiple serial records, malformed and
  oversized record recovery, box coordinate scaling, invalid/empty detection
  handling, image/results display and disconnect cleanup, and invalid JPEG /
  module error / stale image cleanup. The viewer tests use a simulated serial
  source and image decoder; they are not hardware or real JPEG decode tests.
- JavaScript syntax check passed.
- Opened the actual viewer at localhost in the browser and visually checked its
  disconnected layout and controls.
- GitHub was accessed read-only. No original repository files were edited;
  all deliverable files are confined to `VisualOutputTest/`.

## Not yet tested on hardware

The physical XIAO, camera and Grove module were not available in this session.
USB enumeration, real JPEG decoding, exact box alignment for the installed model,
frame rate, I2C transfer reliability and physical D0 behavior must be checked on
Joshua's kit. The firmware build is verified; live hardware operation is not.

Hardware acceptance check:

1. Follow README setup and connect the viewer. Verify the image matches the camera.
2. Move a person into/out of view. Check confidence, box alignment and D0 state.
3. Confirm empty detections clear previous results.
4. Disconnect/reconnect USB; confirm the viewer clears its old image.
5. If capture fails, confirm a visible error, reset XIAO and reconnect.
6. Re-upload the original `Test` sketch to restore its autonomous polling behavior.

## Re-run software tests

With Node.js installed, from this folder:

```sh
node tests/protocol.test.cjs
node tests/viewer.test.cjs
node --check viewer/viewer.js
pio run -e seeed_xiao_esp32c3
```

Node is needed only for these development tests, not for using the viewer.
