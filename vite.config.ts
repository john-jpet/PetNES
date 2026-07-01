import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
  build: {
    target: "es2022",
  },
  server: {
    port: 5173,
    headers: {},
  },
  optimizeDeps: {
    exclude: ["./wasm-out/petnes-core.js"],
  },
  assetsInclude: ["**/*.wasm"],
});
