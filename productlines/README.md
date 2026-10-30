# Product line knowledge base

This directory keeps vendor and model-specific knowledge separate from the
generic ESP32 transport, BDI runtime, MQTT gateway, and frontend.

```text
productlines/
└── <vendor>/
    └── <device-type>/
        └── <product-line>/
            ├── product.json       # Stable product identity and compatibility
            ├── README.md          # Product-line entry point
            ├── docs/              # Official-document index, not copied manuals
            ├── commands/          # Read-only collection and opt-in config
            ├── logs/              # Raw samples and normalized event catalog
            ├── parser/            # Future product-specific parser
            └── simulator/         # Future product-specific generator
```

Rules:

1. Keep raw vendor messages unchanged in `logs/` and add normalization as
   separate metadata.
2. Label generated samples as synthetic. Never present them as captured logs.
3. Separate read-only commands from commands that change device state.
4. Record the software release because commands and message mnemonics can
   change between releases.
5. Do not store credentials, serial numbers, management IPs, or customer data.
6. Link to official manuals instead of committing third-party copyrighted
   manuals to this repository.

The existing `test/h3c_server_sim.py` remains the UniServer/HDM proof of
concept. A product-specific parser or simulator can be connected later without
moving or breaking that code.
