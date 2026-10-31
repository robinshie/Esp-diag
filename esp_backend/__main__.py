import argparse
import logging

import uvicorn

from .api import create_app
from .settings import Settings


def main():
    parser = argparse.ArgumentParser(description="ESP32 MongoDB registry/API and durable MQTT archive")
    parser.add_argument("--config", required=True)
    parser.add_argument("--env-file", default="deploy/mongodb.env")
    args = parser.parse_args()
    settings = Settings.load(args.config, args.env_file)
    uvicorn.run(create_app(settings), host=settings.host, port=settings.port, log_level="info", access_log=False)


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    main()
