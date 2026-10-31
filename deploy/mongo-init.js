const app = db.getSiblingDB(process.env.MONGO_INITDB_DATABASE);
app.createUser({
  user: process.env.ESP_MONGO_USERNAME,
  pwd: process.env.ESP_MONGO_PASSWORD,
  roles: [{role: "readWrite", db: app.getName()}]
});
