"""Environment-backed credentials; importing this module never opens a transport."""
import os


class ConfigError(ValueError):
    pass


def credential(section, key="password_env"):
    name = section.get(key)
    if not name:
        return None
    if not isinstance(name, str) or not os.environ.get(name):
        raise ConfigError(f"missing environment variable: {name}")
    return os.environ[name]
