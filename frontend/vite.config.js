import { defineConfig } from "vite";
import { fileURLToPath } from "node:url";

export default defineConfig({
  server: {proxy: {"/api": {target: process.env.ESP_API_UPSTREAM || "http://127.0.0.1:8080", changeOrigin: true}}},
  build: {
    rollupOptions: {
      input: {
        dashboard: fileURLToPath(new URL("./index.html", import.meta.url)),
        console: fileURLToPath(new URL("./console.html", import.meta.url)),
        history: fileURLToPath(new URL("./history.html", import.meta.url))
      }
    }
  }
});
