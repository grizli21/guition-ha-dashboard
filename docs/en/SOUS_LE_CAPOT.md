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
- **Panel at 17 MHz (60 frames/s): XIP is what holds it.** Flash and PSRAM share the same bus.
  Without XIP, every flash write (Wi-Fi, settings, OTA) briefly cuts PSRAM off, and code fetched from flash
  (4 lines) keeps that bus busy longer than if it were fetched from PSRAM (8 lines). The screen's DMA reads
  PSRAM continuously: it runs short of data and the picture drifts or jumps. With XIP
  (`CONFIG_SPIRAM_XIP_FROM_PSRAM`, `FETCH_INSTRUCTIONS`, `RODATA` in [platformio.ini](../../platformio.ini)),
  the program is copied to PSRAM at boot and the problem goes away — the fix recommended by
  [Espressif](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html).
  A/B test at 17 MHz, XIP being the only change: without XIP the picture jumps badly, with XIP it is clean.
  Measured cost: **2.55 MB of PSRAM** (heap 8.39 → 5.83 MB). At 12 MHz without XIP the picture already held,
  but from 14 MHz on it drifted. On top of that, LVGL is synced to the screen's buffer swap
  (`LV_DEF_REFR_PERIOD 2`, touch read every 10 ms, I2C at 400 kHz): at 20 ms its pace fell on the panel's
  (20.3 ms at 14 MHz) and made the picture stutter — a real effect, but small next to XIP.
- **Settings now live in raw flash, not in NVS (13 Nov).** An NVS write — about fifteen flash
  operations, **3.5 to 6 ms measured** by intercepting the `esp_partition_*` calls — glitched the
  picture at 16 and 17 MHz, **seen by eye**; not at 15 MHz. A raw 32-byte write (**0.2 ms**), a burst
  of 20, or erasing a sector (**34 ms**) do not glitch it, even at 17 MHz. The exact cause inside NVS
  **was never found** — two hypotheses were measured and ruled out: PSRAM blocked during the write
  (CPU throughput unchanged) and cache invalidation (nothing was mapped onto the NVS area). The
  screen, power, radar and battery settings fit in a 32-byte record + CRC16, appended in the **last
  16 sectors (64 kB)** of the `spiffs` partition (unused since LVGL, erased at first boot if it holds
  anything else); a power cut loses nothing: the sector holding the last valid record is never
  erased. Found again after a reboot: **7 ms**. NVS now only serves Wi-Fi and radio calibration.
  With settings in raw flash, the panel bus holds 17 MHz with no glitch seen.
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
- **The dark ring and the dimmed ring are also precomputed and dithered (13 Nov).** At a 19 % mix
  (dark tint), RGB565 keeps only a few tints: the ring's gradient advanced in uneven steps (usage
  feedback). The whole dark ring (270° of gradient) is computed **once**, continuous gradient dithered
  Bayer 4×4, drawn **under** the segments (320 × 320 planar RGB565A8 = **300 kB of PSRAM**); the
  dimmed level uses the same image as a **texture** for the dimmed arcs (`lv_draw_arc img_src`) —
  **2 × 300 kB** in total. Page redraw measured on the screen: **103 → 98-100 ms** per frame; the real
  motivation is appearance. If the allocation fails, dark segments are drawn flat as before. These
  precomputed images (two rings, glow, palette) stay in PSRAM forever: **about 900 kB**, by design.
- **The “AI” page: the sky comes from Home Assistant and only repaints the band that changed.**
  Sun and moon travel along an arc recomputed every second from the timestamps of `maison/ciel`; the
  moon phase is drawn in cells from its age; the sky background is a **16-px column repeated as
  tiles** (`LV_IMAGE_ALIGN_TILE`); rain falls in **fixed 4-px corridors**, one drop per corridor, the
  column changing at each fall. Two LVGL patches make it tenable:
  [lvgl_9.6.0_tuiles_zone_utile.patch](../patches/lvgl_9.6.0_tuiles_zone_utile.patch) only decodes the
  tiles touching the area to repaint — measured on screen, a 4-px band (11,700 px) goes from **60 to
  29 ms** per frame (380 one-pixel tiles used to be decoded each time) — and
  [lvgl_9.6.0_elagage_parcours.patch](../patches/lvgl_9.6.0_elagage_parcours.patch) skips objects
  outside the area before any style lookup, plus the border when the area only touches the object's
  inside: under rain, **−1.1 ms per frame, 42 → 40 % of CPU time** (measured A-B-A; simulator output
  pixel-identical). LVGL's invalidation buffer holds **32 areas** (`LV_INV_BUF_SIZE`, default): beyond
  that LVGL repaints the whole screen (**151 ms** measured) — hence the narrow corridors (at most 20)
  and one area per corridor.
- **The camera gives everything back on close.** The MJPEG image buffers (2 × 460,800 B), the JPEG
  buffers (3 × 250,000 B), the decode strip, the element-capture layers and the background task
  stacks are freed when the camera closes: **1.67 MB of buffers** — measured on the screen, free
  PSRAM **4.07 → 5.88 MB** and internal RAM **87 → 113 kB** right at closing. The background tasks
  delete themselves, never killed from the outside: we would catch them in the middle of `recv()`.
  On the stream side: the TCP window raised from 5,760 B to **32 kB** gave **15.3 to 15.9 distinct
  frames per second** (4.5 at the start); an MJPEG stream never closed saturated **22 of the 32
  Wi-Fi receive buffers** and left the screen deaf (MQTT and bench mute) — the clean
  open/close of the stream is a fix visible in [src/ui_lvgl/ui_camera.cpp](../../src/ui_lvgl/ui_camera.cpp).
- **Camera photo overlays are retaken only on change (13 Nov).** The elements laid over the camera
  (texts, gauges from other pages) are photographed (`lv_snapshot` ARGB8888) so the MJPEG stream does
  not erase them. They used to be retaken **every 300 ms**, changed or not, the old photo kept for 1 s
  (up to **24 alive**): PSRAM hit zero on the 3D Printer page, camera empty, when the printer started.
  Each element now carries a **fingerprint** (position, visibility, texts, images, arc and bar
  values): the photo is retaken only if the fingerprint changes, or at the latest every **5 s**
  (fallback for a change the fingerprint cannot see). Measured in use: 2 live photos, 16 kB.
  30-second trace: live photos, bytes, free PSRAM.
- **Radar watchdog (13 Nov).** A battery-powered screen had not slept since 4:49 am: the LD2410S
  radar reported **permanent** presence (7.1 m, pin high, thresholds unchanged) — rereading its
  configuration (`/radar/config`) unblocked it within 2 s. Hence: presence uninterrupted for **1 h**
  AND no touch for **1 h** → reread the configuration (blocks the loop for **~1.6 s**, nobody is
  touching the screen), **at most once per hour**; one line to the HA log before, one **15 s after**
  (presence still there = real presence; gone = the radar was stuck).
- **LVGL 8.4 → 9.6 migration: lessons recorded in the code.** First
  compile under 9.6: **295 errors in 40 sources**. The vendored library now carries **five patches**
  ([patches/](../patches/)): the scrolling fast path, tiles limited to the useful area and the
  per-area traversal pruning (applied), the invalidation probe and the stack depth measurement
  (guarded by compile flags, not applied). The pitfalls encountered, all traced in
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
