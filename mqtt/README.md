# Local MQTT broker

Development-only Mosquitto broker for the ESP32/H3C BDI test.

```bash
cd Esp-diag/mqtt
docker compose up -d
docker compose logs -f
```

Broker URL from this Mac: `mqtt://localhost:1883`

Development credentials are `esp32` / `esp32-bdi-test`. The broker rejects
anonymous clients. The port is bound to localhost because Python acts as the
USB-to-MQTT gateway.

Useful test commands:

```bash
docker exec esp-mosquitto mosquitto_sub -h localhost -u esp32 \
  -P esp32-bdi-test -t 'h3c/#' -v
docker exec esp-mosquitto mosquitto_pub -h localhost -u esp32 \
  -P esp32-bdi-test -t 'h3c/test' -m 'hello mqtt'
```

Stop the broker without deleting its data:

```bash
docker compose down
```

Authentication is enabled, but traffic is not encrypted. Add TLS before using
this broker outside a trusted test network.
