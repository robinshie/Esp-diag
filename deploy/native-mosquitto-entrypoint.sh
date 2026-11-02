#!/bin/sh
set -eu
: "${MQTT_TOPIC_ROOT:?Set MQTT topic root}"
: "${ESP_DEVICE_MQTT_PASSWORD:?Set device password}"
: "${ESP_ARCHIVE_MQTT_PASSWORD:?Set archive password}"
: "${ESP_DASHBOARD_MQTT_PASSWORD:?Set dashboard password}"
for value in "$MQTT_TOPIC_ROOT" "$ESP_DEVICE_MQTT_USERNAME" "$ESP_ARCHIVE_MQTT_USERNAME" "$ESP_DASHBOARD_MQTT_USERNAME"; do
    case "$value" in ''|*[!A-Za-z0-9_.-]*) echo 'Invalid MQTT namespace or username' >&2; exit 1 ;; esac
done
if [ "$ESP_DEVICE_MQTT_USERNAME" = "$ESP_ARCHIVE_MQTT_USERNAME" ] || [ "$ESP_DEVICE_MQTT_USERNAME" = "$ESP_DASHBOARD_MQTT_USERNAME" ] || [ "$ESP_ARCHIVE_MQTT_USERNAME" = "$ESP_DASHBOARD_MQTT_USERNAME" ]; then
    echo 'Device, archive and dashboard identities must differ' >&2; exit 1
fi
umask 077
mkdir -p /tmp/esp-mqtt
rm -f /tmp/esp-mqtt/passwd
mosquitto_passwd -b -c /tmp/esp-mqtt/passwd "$ESP_DEVICE_MQTT_USERNAME" "$ESP_DEVICE_MQTT_PASSWORD"
mosquitto_passwd -b /tmp/esp-mqtt/passwd "$ESP_ARCHIVE_MQTT_USERNAME" "$ESP_ARCHIVE_MQTT_PASSWORD"
mosquitto_passwd -b /tmp/esp-mqtt/passwd "$ESP_DASHBOARD_MQTT_USERNAME" "$ESP_DASHBOARD_MQTT_PASSWORD"
{
    printf 'user %s\n' "$ESP_DEVICE_MQTT_USERNAME"
    for channel in report status availability; do printf 'topic write %s/+/%s\n' "$MQTT_TOPIC_ROOT" "$channel"; done
    for username in "$ESP_ARCHIVE_MQTT_USERNAME" "$ESP_DASHBOARD_MQTT_USERNAME"; do
        printf '\nuser %s\n' "$username"
        for channel in report status availability; do printf 'topic read %s/+/%s\n' "$MQTT_TOPIC_ROOT" "$channel"; done
    done
} > /tmp/esp-mqtt/acl
chown -R mosquitto:mosquitto /tmp/esp-mqtt /mosquitto/data
exec mosquitto -c /mosquitto/config/mosquitto.conf
