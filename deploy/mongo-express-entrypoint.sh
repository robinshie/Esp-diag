#!/bin/sh
set -eu
# Encode credentials at startup so punctuation in a configured password never
# changes the URI. The resulting secret stays in the container environment.
ME_CONFIG_MONGODB_URL=$(node -e '
const e = process.env;
process.stdout.write("mongodb://" + encodeURIComponent(e.ESP_MONGO_USERNAME) + ":" +
  encodeURIComponent(e.ESP_MONGO_PASSWORD) + "@mongodb:27017/" +
  encodeURIComponent(e.ESP_MONGO_DATABASE) + "?authSource=" + encodeURIComponent(e.ESP_MONGO_DATABASE));
')
export ME_CONFIG_MONGODB_URL
exec /docker-entrypoint.sh mongo-express
