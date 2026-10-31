#!/usr/bin/env python3
"""Optional isolated MQTT 3.1.1 test broker. Binds only to loopback.

Install test/requirements-integration.txt in a disposable environment first.
This does not replace the production Mosquitto configuration.
"""
import argparse
import asyncio
import logging
from amqtt.broker import Broker


async def run(port, ws_port=None):
    listeners = {"default": {"type": "tcp", "bind": f"127.0.0.1:{port}"}}
    if ws_port:
        listeners["websocket"] = {"type": "ws", "bind": f"127.0.0.1:{ws_port}"}
    broker = Broker({"listeners": listeners,
                     "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}}})
    await broker.start()
    print(f"TEST MQTT READY 127.0.0.1:{port}", flush=True)
    if ws_port:
        print(f"TEST MQTT WEBSOCKET READY ws://127.0.0.1:{ws_port}/mqtt", flush=True)
    try:
        await asyncio.Event().wait()
    finally:
        await broker.shutdown()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", type=int, default=18883)
    p.add_argument("--ws-port", type=int, help="optional loopback WebSocket listener for the frontend")
    logging.basicConfig(level=logging.ERROR)
    try:
        args = p.parse_args()
        asyncio.run(run(args.port, args.ws_port))
    except KeyboardInterrupt:
        pass
