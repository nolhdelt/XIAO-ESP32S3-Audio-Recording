# Project status memo

Read this first if the user says "I plugged it back in" or otherwise
resumes this project after a break. It summarizes what's built, what's
actually working (tested end-to-end), and the things that tripped us up
last time so they don't get re-debugged from scratch.

## Who the user is

New to Arduino, Python, and terminals in general — never coded before.
Explanations should assume zero background: say which window to type
something in, what it does in plain English, and what success looks
like. Don't assume they remember terminology from earlier in a session.

## What this project is

A Seeed **XIAO ESP32S3 Sense** records audio on a button press and
uploads the WAV file over WiFi to the user's laptop, where a small
Python server saves it to disk.

## Hardware

- Seeed XIAO ESP32S3 **Sense** (the Sense expansion board is required
  for the onboard mic + PSRAM; the plain XIAO ESP32S3 doesn't have
  these)
- **No SD card** — the user doesn't have one. Audio is buffered in the
  board's onboard PSRAM instead (max 60 sec per clip by default).
- **No external button** — uses the XIAO's built-in **BOOT** button
  (GPIO0) instead.
- User connects via their **laptop's mobile hotspot** (not a home
  router). Hotspot must be forced to **2.4GHz** (Windows: Settings >
  Network & Internet > Mobile hotspot > Network band).

## The firmware variant actually in use

`firmware/XIAO_ESP32S3_Audio_Recorder_NoSD/XIAO_ESP32S3_Audio_Recorder_NoSD.ino`

- Press BOOT once: starts recording into PSRAM
- Press BOOT again: stops, builds a WAV file in memory, uploads it via
  one HTTP POST to `SERVER_URL` (set in `config.h`, gitignored)
- If upload fails, the recording is lost — there's no SD fallback

Two other firmware variants exist in the repo but are **not** what the
user is using day-to-day:
- `firmware/XIAO_ESP32S3_Audio_Recorder/` — same idea but writes to an
  SD card first, for if the user ever gets a microSD card
- `firmware/XIAO_ESP32S3_Live_Stream/` — a different, unused direction:
  continuous live audio streaming to a browser over WebSocket (BOOT
  toggles streaming instead of record-then-upload). Pairs with
  `server/live_audio_server.py` and `server/listen.html`, port 8765.
  The user explored this early on but settled on the record-and-upload
  approach instead.

## The receiver (Python side)

`server/receiver.py` — Flask app, listens on port **5000**, path
`/upload`. Saves uploaded files into `server/recordings/`. Must be
**running** (either via `python receiver.py` in PowerShell, or by
double-clicking the file — both work now that Flask is installed) at
the exact moment the user presses BOOT the second time, or the upload
fails with "connection refused" and the recording is lost.

Install step (one-time): `pip install -r requirements.txt` (or
`pip install flask` directly) from inside the `server` folder.

## Known gotchas already hit and fixed

1. **I2S library mismatch (fixed, pushed).** `arduino-esp32` core 3.x
   renamed the Arduino I2S wrapper from `I2S.h`/`PDM_MONO_MODE` (core
   2.0.x) to `ESP_I2S.h`/`I2S.setPinsPdmRx()`/`I2S_MODE_PDM_RX`. All
   three sketches were updated to the core-3.x API and this is
   confirmed working — compiles and runs.

2. **Hotspot IP changes.** `SERVER_URL` in `config.h` must match
   whatever IP `receiver.py` prints *at that moment* — this is NOT a
   fixed number. On Windows Mobile Hotspot it's almost always
   `192.168.137.1`, but if the laptop is on regular WiFi instead of the
   hotspot, it'll be a totally different address (e.g. `10.3.26.11` or
   `10.8.20.150` were both seen). **Always re-check the IP printed by
   `receiver.py` before assuming `config.h` is still correct**,
   especially if the user switched between hotspot and regular WiFi
   between sessions.

3. **Don't confuse the XIAO's own IP with the computer's IP.** Serial
   Monitor prints "WiFi connected, IP: ..." — that's the **XIAO's**
   address, not the server's. `SERVER_URL` needs the **computer's**
   address (from `receiver.py`'s output), not this one. This caused a
   failed-upload loop once already (user had accidentally put the
   XIAO's own IP into `SERVER_URL`).

4. **Hotspot has to actually be toggled on**, not just configured —
   caused a "WiFi connection failed" loop once (hotspot was off).

5. **Typing `SERVER_URL` by hand is error-prone.** User twice dropped a
   `/` (typed `http:/` instead of `http://`) and once omitted `:5000`
   entirely when retyping it manually. If walking through this again,
   have them delete the whole line and type the full replacement
   carefully, or paste.

6. **Pasting multi-word commands into PowerShell was splitting them**
   across separate command lines early on (e.g. `pip install flask`
   became two separate commands, `pip` then `install flask`, causing
   `'install' is not recognized` errors). Typing manually fixed it.
   Also got stuck in the Python interactive shell (`>>>`) a couple of
   times by typing `python` alone — had to use `exit()` to get back to
   a normal prompt.

## Last confirmed-working state

Full end-to-end test succeeded: BOOT press → 32.2 sec recording →
buffered in PSRAM → uploaded to `http://192.168.137.1:5000/upload` →
`receiver.py` responded `200` and saved `rec_97282.wav` into
`server/recordings/`. This was on the laptop's hotspot, with both
`config.h` and `receiver.py`'s printed IP matching.

## Repo / branch info

- Repo: `nolhdelt/XIAO-ESP32S3-Audio-Recording`
- Working branch: `claude/esp32s3-audio-wifi-recorder-mrfoki`
- Open PR: https://github.com/nolhdelt/XIAO-ESP32S3-Audio-Recording/pull/1
  (merges into `main`, not yet merged as of this writing)
- User has a local copy extracted from a ZIP download of this branch,
  under `C:\Users\nolh2\OneDrive\Documents\Arduino\XIAO-ESP32S3-Audio-Recording-claude-esp32s3-audio-wifi-recorder-mrfoki\`
  — if more firmware fixes get pushed, the user needs to re-download
  the ZIP (or `git pull`, but they've only used the ZIP method so far)
  to get them locally.

## Open items / possible next steps (not started)

- User asked about letting the board join "any WiFi" without
  re-flashing (WiFiManager-style captive portal setup). Explained the
  concept and offered to build it; **not yet built**, waiting on user
  to say go.
- 5GHz WiFi was asked about and explained as a hardware limitation of
  the ESP32-S3 chip (not fixable in software) — no action needed,
  just context if it comes up again.
