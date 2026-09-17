"""
Minimal HTTP receiver for the XIAO ESP32S3 audio recorder.

The firmware POSTs the raw WAV file body to /upload with an
X-Filename header naming the file. This script saves each upload
into ./recordings/.

Run with:
    pip install -r requirements.txt
    python receiver.py

Then set SERVER_URL in the firmware's config.h to:
    http://<this-machine's-LAN-IP>:5000/upload
"""

import os
from datetime import datetime

from flask import Flask, request

app = Flask(__name__)
SAVE_DIR = os.path.join(os.path.dirname(__file__), "recordings")
os.makedirs(SAVE_DIR, exist_ok=True)


@app.route("/upload", methods=["POST"])
def upload():
    filename = request.headers.get("X-Filename")
    if not filename:
        filename = f"recording_{datetime.now():%Y%m%d_%H%M%S}.wav"
    filename = os.path.basename(filename)  # prevent path traversal

    dest_path = os.path.join(SAVE_DIR, filename)
    with open(dest_path, "wb") as f:
        f.write(request.get_data())

    size = os.path.getsize(dest_path)
    print(f"Saved {dest_path} ({size} bytes)")
    return {"status": "ok", "saved_as": filename, "bytes": size}, 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
