# H3C S5120V2-LI

This product line is a fixed-port Comware 7 Ethernet switch family. It must be
handled separately from the UniServer/HDM server logs used by the current
Python simulator.

## Directory contents

- `docs/official-manuals.md` — official H3C product, installation,
  configuration, command, software, and log-reference links.
- `docs/quick-start.zh-CN.md` — 中文接线、巡检和日志采集速查。
- `commands/read-only.txt` — safe inventory, health, Layer 2, and log
  collection commands.
- `commands/syslog-template.txt` — opt-in configuration template for a remote
  Syslog receiver.
- `logs/samples.log` — synthetic instances of official Comware message formats.
- `logs/catalog.json` — initial BDI-oriented normalization catalog.
- `simulator/fake_ssh_server.py` — local SSHv2 fake switch with Comware command
  responses and a dynamic log buffer.
- `test/s5120v2_ssh_client.py` — repository-level automated SSH command and
  interactive-shell test client.

## Before using on a real switch

Run these commands first and retain their output with secrets removed:

```text
display version
display device manuinfo
display current-configuration | include version
```

Select the matching manual by software release. R6308P01 and later use the
R63xx fixed-port switch log reference. Earlier releases can use the dedicated
S5120V2-LI 6W100 log reference.

All command files use placeholders. Do not paste a configuration template
until the management path, destination IP, UDP port, and rollback plan have
been confirmed.
