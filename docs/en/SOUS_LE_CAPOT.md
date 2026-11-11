# Under the hood

**English** | [Français](../SOUS_LE_CAPOT.md)

← [Back to README](../../README.md)

All figures quoted below were measured, either on the screen (PC bench ⇄ screen, A-B-A runs,
captures compared byte to byte) or in the simulator. Only the mechanisms and the trace of their
measurement in comments are published in this repository; the full campaign logs stayed out of the
public repository.

- **“Banded” scrolling, the patched LVGL.** When a container scrolls, LVGL repaints its whole
  area: measured in stock 9.6, **175,336 px per gesture step** (the entire 404 × 434 container). The
  [scroll_bande_9.6.patch](../patches/scroll_bande_9.6.patch), applied to the vendored library
  (marker `.bande-applique`), offsets the image memory by the displacement actually applied and
  repaints only two strips, entry and exit: on the screen, **7,676 px and 61 → 9 ms** median
  frame (bench command `/defiler`, 30 steps, 180 measurements). A full-area repair frame starts
  150 ms after the last step. Eligibility is decided when the container is created
  ([src/ui_lvgl/ui_theme.cpp](../../src/ui_lvgl/ui_theme.cpp), `ui_defilement_rapide`): an exclusion
  during a gesture does not undo the frames already skipped — measured for a time in 8.4 at 84
  frames / 3,712,609 px of orphaned pixels.
- **Rotation 0, two image buffers in PSRAM.** The whole production runs at rotation 0: the
  LovyanGFX driver, **modified** for the purpose (it originally has a single image buffer: see
  [lovyangfx_double_tampon_1.3.0.patch](../patches/lovyangfx_double_tampon_1.3.0.patch)), allocates two
  full-screen imagebuffers, LVGL draws into the one that is not shown, the panel flips at VSYNC — no
  more copy to the panel (measured: 51 ms, worst 81, Info page) and **77 kB of internal RAM given
  back** ([src/ecran/ecran.cpp](../../src/ecran/ecran.cpp)). It is the condition for the fast path: with
  no memory to offset, nothing to offset. The direct mode with a single buffer was tried and rejected
  in use (flicker and tearing).
- **The thermostat ring: 75 → 24 ms per step.** The ring is described element by element (135
  segments of 2°, faded, rounded ends, the room dot), each with its own impact rectangle;
  only the area that changes is invalidated (`anneau_invalider`). Measured on the screen in A-B-A
  during a setpoint drag in steps of 0.5°: **75.5 → 24.0 ms** median per step (−68 %),
  **108,900 → about 32,500 px** repainted per step. The simulator validated pixel identity before/after.
- **The same pattern on the charts.** Finger reading of the 24 h chart used to repaint **119,086 px
  in 103 ms** per finger step (measured); repainting only the two cursor columns and the bubble's old
  place brought it down to about 45 ms. Same on the curves: tank **106 → 30 ms**, stove
  **51 → 24 ms** per frame (screen, A/B).
- **The heating glow is dithered and precomputed.** The red halo under the ring is a
  232 × 232 px image (**161,472 B**) computed once, RGB565 + planar alpha mask, in **ordered
  dithering (Bayer 4×4)**: RGB565 only offers some twenty red levels, without dithering the steps
  are visible. It is an appearance choice, not a speed one.
- **The camera gives everything back on close.** The MJPEG image buffers (2 × 460,800 B), the JPEG
  buffers (3 × 250,000 B), the decode strip, the element-capture layers and the background task
  stacks are freed when the camera closes: **1.67 MB of buffers** — measured on the screen, free
  PSRAM **4.07 → 5.88 MB** and internal RAM **87 → 113 kB** right at closing. The background tasks
  delete themselves, never killed from the outside: we would catch them in the middle of `recv()`.
  On the stream side: the TCP window raised from 5,760 B to **32 kB** gave **15.3 to 15.9 distinct
  frames per second** (4.5 at the start); an MJPEG stream never closed saturated **22 of the 32
  Wi-Fi receive buffers** and left the screen deaf (MQTT and bench mute) — the clean
  open/close of the stream is a fix visible in [src/ui_lvgl/ui_camera.cpp](../../src/ui_lvgl/ui_camera.cpp).
- **LVGL 8.4 → 9.6 migration: three patches, lessons recorded in the code.** First
  compile under 9.6: **295 errors in 40 sources**. The vendored library carries three patches
  ([patches/](../patches/)): the scrolling fast path (applied), the invalidation probe and the stack
  depth measurement (guarded by compile flags, not applied). The pitfalls encountered, all traced in
  comments in the sources: 9.6 no longer takes its clock from the configuration — without
  `lv_tick_set_cb`, nothing refreshes anymore; endianness — without `LV_COLOR_FORMAT_RGB565_SWAPPED`
  on the screen and the two buffers, red and blue are swapped on the panel; `lodepng_decode32` no
  longer returns pixels but a descriptor — misread, it cost **4.44 MB of PSRAM** lost, recovered;
  the allocator only sends to PSRAM blocks larger than **512 B**
  (`heap_caps_malloc_extmem_enable`), otherwise the internal heap chokes.
- **The PC simulator does not lie, but it does not keep time.** It compiles the REAL page
  sources, with the same configuration, and produces about 280 captures at 480 × 480: pixel identity in
  the byte-to-byte comparison between two binaries, draw calls and invalidated pixels counted per
  primitive, the full scenario under AddressSanitizer/UBSan (zero reports). But its microseconds are
  PC time, **300 to 600 times** faster than the screen: all the README figures that talk about time
  are measured on the screen.
- **Watchdog, dump of the previous crash, test bench.** The loop watchdog is at **10 s**:
  a frozen screen restarts and says where it was — the current step, its checkpoints and the last
  traces (3,072 B ring) survive in RTC memory and are shown on the Info page. The PC bench only
  answers the designated machine: HTTP **8080** (screen capture, simulated taps and drags), Telnet
  **23** (log, dump of the last crash, without restarting). Non-negotiable lock: any synthetic tap
  that would touch a clickable object is refused (**403**) — hence the `/defiler` route, which
  scrolls without ever simulating a finger.
