import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

export default defineConfig({
  plugins: [solid()],
  server: {
    // Fixed and distinct from the marketing site's 43210, so both dev
    // servers can run side by side.
    port: 43220,
  },
});
