# SSH simulator

`fake_ssh_server.py` is a local-only SSHv2 test double for an H3C
S5120V2-LI switch. It accepts commands over a real SSH transport and returns
synthetic Comware-style output and logs.

```bash
python3 -m pip install -r test/requirements.txt
python3 productlines/h3c/switches/s5120v2-li/simulator/fake_ssh_server.py
```

Connect manually:

```bash
ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -p 2222 admin@127.0.0.1
```

Password: `h3c-test`.

Run the automated client in another terminal:

```bash
python3 test/s5120v2_ssh_client.py
```

After flashing the ESP32 firmware, run the complete simulated control loop:

```bash
python3 test/s5120v2_bdi_e2e.py
```

This test injects an interface-down event, sends the normalized event to the
ESP32 over USB, waits for the BDI recovery command, applies `undo shutdown`
over SSH, verifies the interface is up, and publishes the lifecycle to MQTT.
It controls only this local fake switch.

The default bind address is `127.0.0.1`. Do not expose the simulator to an
external interface. The credentials, device information, responses, and log
records are synthetic.

Do not replace `test/h3c_server_sim.py`: that script remains the separate
UniServer/HDM server proof of concept.
