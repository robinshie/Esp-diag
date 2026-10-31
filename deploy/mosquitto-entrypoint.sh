#!/bin/sh
set -eu
: "${MQTT_PASSWORD:?MQTT password must be configured}"
for value in "${MQTT_USERNAME:-}" "${MQTT_TOPIC_ROOT:-}"; do
    case "$value" in
        ''|*[!A-Za-z0-9_.-]*) echo 'MQTT username and topic root must use letters, numbers, _, . or -' >&2; exit 1 ;;
    esac
done
umask 077
mkdir -p /tmp/esp-mqtt
# Rebuild generated credentials on every container start. Newer passwd tools
# refuse -c when the previous container's temporary file already exists.
rm -f /tmp/esp-mqtt/passwd
mosquitto_passwd -b -c /tmp/esp-mqtt/passwd "$MQTT_USERNAME" "$MQTT_PASSWORD"
printf 'user %s\ntopic readwrite %s/#\n' "$MQTT_USERNAME" "$MQTT_TOPIC_ROOT" > /tmp/esp-mqtt/acl
chown -R mosquitto:mosquitto /tmp/esp-mqtt /mosquitto/data
exec mosquitto -c /mosquitto/config/mosquitto.conf
