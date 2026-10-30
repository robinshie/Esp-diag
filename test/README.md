# Continuous H3C USB-to-MQTT fault simulation

The simulator sends a deterministic smoke cycle over USB, then continuously
emits random H3C health and fault logs. ESP32 runs the BDI decisions and sends
results back over USB. Python forwards every input and BDI result to MQTT and
sends a simulated acknowledgement for unlock requests.

```bash
cd Esp-diag
python3 -m pip install -r test/requirements.txt
python3 test/h3c_server_sim.py
```

Stop with `Ctrl+C`. For a bounded test:

```bash
python3 test/h3c_server_sim.py \
  --duration 60 --min-interval 1 --max-interval 3
```

MQTT topics:

- `h3c/server/sim-01/log`: fault logs only; normal heartbeats stay on USB
- `h3c/server/sim-01/ack`: simulated unlock acknowledgements to ESP32
- `h3c/esp32-s3-eth/availability`: retained online/offline state changes
- `h3c/esp32-s3-eth/health`: retained normal/fault state changes
- `h3c/esp32-s3-eth/status`: retained agent/network status
- `h3c/esp32-s3-eth/event`: BDI belief changes
- `h3c/esp32-s3-eth/action`: BDI actions and results
- `h3c/esp32-s3-eth/cmd/unlock`: simulated unlock requests

Unlock is only a test protocol. It does not access or bypass a real server.

The gateway uses state-and-exception reporting: normal USB heartbeats are not
published one by one. Faults, lock events, BDI decisions, unlock results, and
recovery state changes remain visible in MQTT.

`gateway.sh start` skips the deterministic startup fault cycle. Run
`h3c_server_sim.py` directly when you want the full smoke test.
