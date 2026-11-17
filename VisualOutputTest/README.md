# VisualOutputTest — Joshua's camera and AI viewer

This is a separate PlatformIO project based on `LuxProtocol/AI/Test` at commit
`ea4ecf890205c44ebca7891e4c09df9b8702d001`. Place this entire folder beside `Test`,
not inside it. Nothing in the original `Test/` or repository README is changed.
This delivery is a local test copy; it has not been pushed to GitHub.

## What you will see

A browser shows the actual Grove Vision AI V2 JPEG, boxes and confidence scores,
D0 person-alert status, and inference timings. Classifications and points, when
provided by the model, appear as text. Images and detections are taken from the
same response, so one capture is never overlaid with another capture's results.
The viewer clears old images on errors, disconnects, missing images, or a
20-second stale-frame timeout. No webcam substitution or simulated detections
are used in the viewer.

## Hardware and software

- The existing XIAO ESP32-C3, Grove Vision AI V2, attached camera, I2C wiring and
  already-flashed detection model. Keep their existing working wiring/power.
- A USB **data** cable from the **XIAO ESP32-C3** to your computer.
- PlatformIO (the VS Code extension is fine) to build/upload the test firmware.
- Python 3 to serve the viewer locally, and desktop **Chrome or Edge** with Web
  Serial. Firefox/Safari are not supported by this viewer.

Inference still runs on the Grove module. The XIAO requests a capture, reads it
over I2C, applies the original `class 0 / confidence >= 60` person rule, and
forwards the image/results over USB. Nothing here replaces the Grove model.
Pin D0 still goes HIGH for a qualifying person; it goes LOW on capture errors.
The test captures only while the viewer requests frames. Disconnecting stops
new captures; D0 retains its last state until the next capture/reset. Use this
as a bench visualization test, not as a control-loop replacement.

## Run with PlatformIO in VS Code

1. Extract `VisualOutputTest.zip`. Put `VisualOutputTest` next to your existing
   `Test` directory, or keep it anywhere separate.
2. In VS Code / PlatformIO, **Open Project** and select **VisualOutputTest**.
   Confirm the environment is `seeed_xiao_esp32c3`.
3. Connect the XIAO USB cable. Click **Build**, then **Upload**. This replaces
   the sketch on that XIAO, but does not edit the original source or flash a
   model onto the Grove module.
4. Close PlatformIO's Serial Monitor and any other program using that USB port.
5. Open a terminal in `VisualOutputTest` and run:

   Windows:
   ```powershell
   py -3 -m http.server 8000 --bind 127.0.0.1 --directory viewer
   ```

   macOS/Linux:
   ```sh
   python3 -m http.server 8000 --bind 127.0.0.1 --directory viewer
   ```
6. On that same computer, open **http://localhost:8000** in Chrome or Edge.
7. Click **Connect XIAO**, choose the XIAO USB serial port, and click **Connect**
   in the browser's chooser. Wait up to 15 seconds for the first frame.
8. Point the camera at a person. Compare the displayed image and box, confidence,
   person alert and physical D0 output. Move out of view and verify they update.
   Class 0 is assumed to mean person, exactly as in the original program.
9. Click **Disconnect** before uploading firmware again. Stop the local server
   with Ctrl+C when finished.

The server binds only to your computer. Camera images stay in the browser and
USB connection; the viewer has no upload, analytics or external script calls.

## Equivalent command-line build/upload

From the extracted `VisualOutputTest` folder, with PlatformIO installed:

```sh
pio run -e seeed_xiao_esp32c3
pio device list
pio run -e seeed_xiao_esp32c3 -t upload
```

If PlatformIO cannot choose the port, append `--upload-port COM5` on Windows or
`--upload-port /dev/ttyACM0` on Linux, replacing the example with your XIAO port.
On macOS the port is commonly `/dev/cu.usbmodem...`; use `pio device list`.
Do not open the serial monitor while the viewer is connected.

## Return to the original

Disconnect the viewer. Open the original `Test` project in PlatformIO and upload
it again, or from your repository root run:

```sh
pio run -d Test -e seeed_xiao_esp32c3 -t upload
```

## Exact added files

All paths below are new; no existing repository files were modified:

- `VisualOutputTest/platformio.ini`: isolated XIAO build; pins platform and
  library versions so the tested dependency combination is repeatable.
- `VisualOutputTest/src/main.cpp`: original connection/person/threshold/D0 logic
  adapted to request image-bearing inference responses and forward JSON to USB.
- `VisualOutputTest/viewer/index.html`: local browser viewer page.
- `VisualOutputTest/viewer/viewer.js`: USB connection, image rendering, overlays,
  result display, stale-frame/error handling.
- `VisualOutputTest/viewer/protocol.js`: bounded serial framing and box geometry.
- `VisualOutputTest/tests/protocol.test.cjs`: serial framing/geometry regression tests.
- `VisualOutputTest/tests/viewer.test.cjs`: viewer tests with a simulated USB source.
- `VisualOutputTest/README.md`: this guide and source references.
- `VisualOutputTest/.gitignore`: ignores local build artifacts.
- `VisualOutputTest/VALIDATION.md`: checks performed and hardware limitations.

## Design details and troubleshooting

**This is a frame-by-frame preview, not guaranteed video-rate streaming.** A JPEG
must cross I2C and then USB. The original 200 ms poll interval is a minimum;
large images can take several seconds. The viewer asks for the next capture
only after displaying the previous one, preventing an ever-growing backlog.

**Why not just add `AI.last_image()` to the original?** The inspected SSCMA 1.0.0
library has a short synchronous event timeout and releases its zero-copy JSON
buffer before the image is consumed. This test uses the same library for I2C
transport but a separate bounded response reader. It sends the official
`AT+INVOKE=1,0,0` command (one capture, no difference filter, include image),
keeps the backing buffer alive, and waits up to 15 seconds. The original library
and original project remain untouched.

**No picture, but results appear:** your module/firmware did not include a JPEG.
The viewer explicitly says so and does not reuse an older picture. Verify image
support with Seeed's documentation below. A flashed detection model and a working
camera are required. This project does not automatically update firmware/models.

**Response exceeds 64 KiB / invalid camera JSON:** the test deliberately bounds
memory use for this board. A lower camera JPEG size or fewer detections may be
needed. It allows 64 KiB for the complete JSON response and 16 KiB of JSON
metadata storage. Do not blindly increase these on the ESP32-C3.

**Timeout or stopped capture:** check power, camera and I2C; disconnect the viewer,
reset the XIAO, then reconnect. The firmware stops on capture errors rather than
risk using a late result as if it belonged to a new request.

**Boxes do not line up:** the official box format is center X, center Y, width,
height, confidence, class ID. The default assumes JPEG pixel coordinates. If the
model outputs another coordinate resolution, enter its width/height under
**Overlay coordinates**. A differently cropped/rotated model image needs a
model-specific mapping; disabling overlays still leaves numeric results visible.

**Wrong label:** only class 0 is labeled person, matching the existing program.
Change `PERSON_CLASS_ID` in the test's `src/main.cpp` and upload again if your
model differs. Other labels are shown as class IDs because no label map is in
this repository.

**Port busy / not found:** use a data cable and the XIAO USB port. Close other
serial tools and browser tabs holding it. On Linux, make sure your user has
normal serial-device access. Reconnect after uploads because USB can re-enumerate.

## Sources inspected

- Original program: https://github.com/LuxProtocol/AI/blob/ea4ecf890205c44ebca7891e4c09df9b8702d001/Test/src/main.cpp
- Original configuration: https://github.com/LuxProtocol/AI/blob/ea4ecf890205c44ebca7891e4c09df9b8702d001/Test/platformio.ini
- Seeed image/inference guide: https://wiki.seeedstudio.com/grove_vision_ai_v2_at/
- Pinned library: https://github.com/Seeed-Studio/Seeed_Arduino_SSCMA/tree/v1.0.0
- Official AT protocol: https://github.com/Seeed-Studio/SSCMA-Micro/blob/1.0.x/docs/protocol/at_protocol.md
- Center-based box rendering: https://github.com/Seeed-Studio/SSCMA-Micro/blob/1.0.x/sscma/utility.hpp
