#!/usr/bin/env python3
"""Generate local MongoDB/API secrets once; never overwrite existing credentials."""
from pathlib import Path
import os
import secrets
import shlex
from urllib.parse import quote


def manager_defaults():
    return {"MONGO_EXPRESS_PORT": "8081", "MONGO_EXPRESS_USERNAME": "admin",
            "MONGO_EXPRESS_PASSWORD": secrets.token_urlsafe(24),
            "MONGO_EXPRESS_COOKIESECRET": secrets.token_hex(32),
            "MONGO_EXPRESS_SESSIONSECRET": secrets.token_hex(32)}


def mqtt_defaults():
    return {"MQTT_PORT": "18883", "MQTT_WS_PORT": "9001", "MQTT_USERNAME": "esp32",
            "MQTT_TOPIC_ROOT": "h3c", "ESP_DEMO_MQTT_PASSWORD": "esp32-bdi-test"}


def write_manager_login(directory):
    values = {}
    for line in (directory / "mongodb.env").read_text().splitlines():
        key, separator, value = line.partition("=")
        if separator and (key.strip().startswith("MONGO_EXPRESS_") or key.strip() == "MONGO_DATABASE"):
            words = shlex.split(value, comments=True)
            values[key.strip()] = words[0] if words else ""
    database = quote(values.get("MONGO_DATABASE", "esp_diag"), safe="")
    text = (f"数据库管理：http://127.0.0.1:{values['MONGO_EXPRESS_PORT']}/db/{database}\n"
            f"用户名：{values['MONGO_EXPRESS_USERNAME']}\n密码：{values['MONGO_EXPRESS_PASSWORD']}\n"
            "这是本机数据库管理界面的登录信息。修改登录配置后重新运行初始化脚本并重建管理容器。\n")
    descriptor = os.open(directory / "mongo-express-login.txt", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, "w") as output:
        os.fchmod(output.fileno(), 0o600)
        output.write(text)


def ensure_environment(directory):
    directory = Path(directory)
    destination = directory / "mongodb.env"
    if destination.exists():
        # Fill new dependency settings without rotating existing credentials.
        current = destination.read_text()
        keys = {line.partition("=")[0].strip() for line in current.splitlines() if not line.lstrip().startswith("#")}
        missing = {key: value for key, value in {**manager_defaults(), **mqtt_defaults()}.items() if key not in keys}
        if missing:
            with destination.open("a") as output:
                output.write("\n# Docker dependency defaults\n" + "".join(f"{key}={value}\n" for key, value in missing.items()))
            destination.chmod(0o600)
            print("Added missing Docker dependency settings; existing credentials preserved")
        else:
            print("deploy/mongodb.env exists; credentials left unchanged")
        write_manager_login(directory)
        return
    template = (directory / "mongodb.env.example").read_text()
    for placeholder in ("REPLACE_WITH_RANDOM_ROOT_PASSWORD", "REPLACE_WITH_RANDOM_APP_PASSWORD", "REPLACE_WITH_RANDOM_BROWSER_API_TOKEN",
                        "REPLACE_WITH_RANDOM_MANAGER_PASSWORD", "REPLACE_WITH_RANDOM_COOKIE_SECRET", "REPLACE_WITH_RANDOM_SESSION_SECRET"):
        template = template.replace(placeholder, secrets.token_hex(32))
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w") as output:
        output.write(template)
    write_manager_login(directory)
    print("Created deploy/mongodb.env with random credentials (mode 600)")


def main():
    ensure_environment(Path(__file__).resolve().parent)


if __name__ == "__main__":
    main()
