# Parser extension point

Future S5120V2-LI parsing code belongs here. The parser should extract the
timestamp, sysname, module, severity, mnemonic, location, and content without
modifying the raw message.

The first supported keys should come from `../logs/catalog.json`. Keep parsing
separate from BDI policy so the same parser can feed USB, Ethernet, or MQTT
transports.
