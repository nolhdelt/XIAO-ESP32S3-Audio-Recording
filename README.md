# XIAO ESP32S3 Audio Recorder over WiFi

Push a button to start recording audio on a Seeed **XIAO ESP32S3 Sense**,
push it again to stop, and the recording is saved to the onboard microSD
card and then uploaded over WiFi to a server you control.

> **Important:** the built-in PDM microphone and microSD slot only exist
> on the **Sense** variant (XIAO ESP32S3 + Sense expansion board). The
> plain XIAO ESP32S3 has neither, so this firmware targets the Sense
> board.

## Hardware

- Seeed XIAO ESP32S3 **Sense**
- One momentary push button
- microSD card (FAT32), inserted in the Sense board's slot

### Wiring

| Component | Connection |
|---|---|
| Push button | One leg to `D1` (GPIO2), other leg to `GND`. Internal pull-up is used in software, so no resistor is needed. |
| Microphone | Built into the Sense board — nothing to wire. |
| microSD | Built into the Sense board's slot — nothing to wire. |

If you wire the button to a different pin, change `BUTTON_PIN` at the top
of the `.ino` file.

## Firmware setup (Arduino IDE)

1. Install board support: **Tools > Board > Boards Manager**, install
   **"esp32" by Espressif Systems** (v3.x).
2. Select **Tools > Board > XIAO_ESP32S3**.
3. Enable PSRAM: **Tools > PSRAM > OPI PSRAM** (the Sense board has
   PSRAM; this isn't strictly required by this sketch but is good
   practice for anything audio/camera related on this board).
4. Open `firmware/XIAO_ESP32S3_Audio_Recorder/XIAO_ESP32S3_Audio_Recorder.ino`.
5. Copy `config.h.example` to `config.h` (same folder) and fill in:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `SERVER_URL` — where the file gets uploaded (see below)
6. Insert a FAT32-formatted microSD card into the Sense board.
7. Upload the sketch and open the Serial Monitor at 115200 baud.

## Using it

- Press the button once: recording starts, a new file
  `/rec_<millis>.wav` is created on the SD card and PCM audio
  (16kHz, 16-bit, mono) streams into it.
- Press the button again: recording stops, the WAV header is
  finalized, and the file is uploaded over WiFi via an HTTP POST to
  `SERVER_URL`. On a successful upload (HTTP 200) the local copy is
  deleted from the SD card (set `DELETE_AFTER_UPLOAD` to `0` in
  `config.h` to always keep local copies).
- If WiFi/upload fails, the file simply stays on the SD card — pull the
  card and copy it off manually, or fix connectivity and re-record.

## Where the audio gets sent

The firmware just does an HTTP POST of the raw WAV bytes to whatever URL
you put in `SERVER_URL`. A few concrete options, from simplest to more
involved:

1. **A laptop/Raspberry Pi/server on the same WiFi network (included).**
   Run the provided Flask receiver:
   ```bash
   cd server
   pip install -r requirements.txt
   python receiver.py
   ```
   Find that machine's LAN IP (e.g. `192.168.1.50`) and set
   `SERVER_URL` to `http://192.168.1.50:5000/upload`. Uploaded files
   land in `server/recordings/`. This is the easiest way to get started
   and is what the default `config.h.example` assumes.

2. **A cloud VM / always-on server.** Deploy `receiver.py` (or any
   endpoint that accepts a POST body) to a small cloud instance and set
   `SERVER_URL` to its public address, e.g.
   `https://your-server.example.com/upload`. Add HTTPS/auth (e.g. an API
   key header) if it's exposed to the internet.

3. **Expose your local receiver over the internet with a tunnel**, e.g.
   `ngrok http 5000`, and point `SERVER_URL` at the temporary public URL
   ngrok gives you. Good for testing without deploying anything.

4. **Cloud storage instead of your own server** (S3, Google Cloud
   Storage, Dropbox, etc.). These typically want a pre-signed upload URL
   or an API call with credentials, which isn't something you want to
   embed on the device itself. The usual pattern is: keep a small
   receiver server (like `receiver.py`) as the upload target, and have
   *that* server forward/copy each file into S3/Drive/Dropbox using
   their SDK with your credentials kept server-side.

The device itself doesn't need to know or care which of these you pick —
it only ever talks to `SERVER_URL`.

## Notes / things you may need to adjust

- The I2S pins (`I2S_CLK_PIN=42`, `I2S_DATA_PIN=41`) and SD_MMC pins
  (`7`, `9`, `8`) match Seeed's published pinout for the Sense board's
  built-in mic and SD slot. If a future hardware revision changes these,
  check Seeed's wiki for the current values.
- Audio is recorded mono, 16kHz, 16-bit PCM. Change `SAMPLE_RATE` /
  `BITS_PER_SAMPLE` in the `.ino` if you need something different (the
  microphone supports other PDM rates).
- `config.h` is gitignored so your WiFi credentials don't get committed.
