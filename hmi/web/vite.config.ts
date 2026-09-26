import { defineConfig } from 'vite';

// En desarrollo (npm run dev) Vite sirve la HMI y reenvía /api y /ws al gateway.
// En producción el gateway sirve dist/ directamente.
const gateway = process.env.BALANCIN_GATEWAY ?? 'http://127.0.0.1:8000';

export default defineConfig({
  server: {
    host: true, // accesible desde el celular en la misma red
    proxy: {
      '/api': gateway,
      '/ws': { target: gateway.replace(/^http/, 'ws'), ws: true },
    },
  },
  build: { target: 'es2022', chunkSizeWarningLimit: 600 },
});
