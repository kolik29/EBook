import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { resolve } from "path";

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  base: "/",
  server: {
    proxy: {
      "/books": {
        target: "http://192.168.0.103",
        changeOrigin: true,
      },
      "/disable-wifi": {
        target: "http://192.168.0.103",
        changeOrigin: true,
      },
      "/rotate-display": {
        target: "http://192.168.0.103",
        changeOrigin: true,
      },
      "/refresh-display": {
        target: "http://192.168.0.103",
        changeOrigin: true,
      },
    },
  },
  build: {
    outDir: resolve(__dirname, "../data"),
    emptyOutDir: true,
  },
});
