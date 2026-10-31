from __future__ import annotations

import argparse
import json
import logging
import signal
import sys
from pathlib import Path

from .config import ConfigError, browser_config, load_config


def main():
    parser = argparse.ArgumentParser(description="ESP32 gateway: one codebase, deployment configuration only")
    parser.add_argument("command", choices=["check", "probe", "export-web", "run"])
    parser.add_argument("--config", required=True)
    parser.add_argument("--output", help="export-web destination, e.g. frontend/public/runtime-config.json")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        config = load_config(args.config)
        if args.command == "check":
            print(f"VALID mode={config['mode']} agent={config['agent']['id']} devices={len(config['devices'])} "
                  f"actions_enabled={sum(p['enabled'] for d in config['devices'] for p in d['actions'].values())}")
            print("Credentials, file paths and parser rules validated; no network/device commands executed.")
            return 0
        if args.command == "export-web":
            if not args.output:
                raise ConfigError("export-web requires --output")
            destination = Path(args.output).resolve()
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_suffix(destination.suffix + ".tmp")
            temporary.write_text(json.dumps(browser_config(config), indent=2) + "\n")
            temporary.replace(destination)
            print(f"Browser runtime configuration exported to {destination}; no SSH or gateway credentials included.")
            return 0
        if args.command == "probe":
            if config["mode"] != "ssh":
                raise ConfigError("probe requires mode=ssh")
            from .ssh import SSHSession, collect
            for device in config["devices"]:
                with SSHSession(device["ssh"]) as session:
                    observations = collect(device, session)
                print(json.dumps({"device": device["id"], "checks": [
                    {"component": check["component"], "event": check["event"], "alarm": alarm, "cause": cause}
                    for check, alarm, cause in observations]}, ensure_ascii=False))
            print("READ-ONLY PROBE PASSED: no recovery actions invoked")
            return 0
        from .runtime import build_gateway
        gateway = build_gateway(config)
        for sig in (signal.SIGINT, signal.SIGTERM):
            signal.signal(sig, lambda *_: gateway.stop_event.set())
        logging.info("starting mode=%s agent=%s devices=%s", config["mode"], config["agent"]["id"], len(config["devices"]))
        return gateway.run()
    except ConfigError as exc:
        print(f"CONFIG ERROR: {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        # Do not dump command output or credential-bearing transport exceptions.
        logging.error("gateway stopped: %s", type(exc).__name__)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
