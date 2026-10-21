"""
WebSocket relay server for live XIAO ESP32S3 audio streaming.

The XIAO connects to /source and sends raw PCM16 audio chunks. Any
browser tabs connected to /listen receive those same chunks, relayed
in real time, and play them back (see listen.html).

Run with:
    pip install websockets
    python live_audio_server.py

It listens on ws://0.0.0.0:8765 - reachable from the XIAO at
ws://<this-machine's-hotspot-or-LAN-IP>:8765/source, and from a
browser on this same machine at ws://localhost:8765/listen.
"""

import asyncio

import websockets

listeners = set()


async def handler(websocket):
    path = websocket.path

    if path == "/source":
        print("XIAO connected. Streaming live.")
        try:
            async for message in websocket:
                stale = set()
                for listener in listeners:
                    try:
                        await listener.send(message)
                    except websockets.ConnectionClosed:
                        stale.add(listener)
                listeners.difference_update(stale)
        finally:
            print("XIAO disconnected.")

    elif path == "/listen":
        listeners.add(websocket)
        print("Listener connected.")
        try:
            await websocket.wait_closed()
        finally:
            listeners.discard(websocket)
            print("Listener disconnected.")

    else:
        await websocket.close()


async def main():
    async with websockets.serve(handler, "0.0.0.0", 8765):
        print("Listening on ws://0.0.0.0:8765 ...")
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    asyncio.run(main())
