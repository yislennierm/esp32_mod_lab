import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  build: {
    chunkSizeWarningLimit: 700,
    rollupOptions: {
      output: {
        manualChunks(id) {
          if (id.indexOf('node_modules') === -1) return undefined;
          if (id.indexOf('pixi.js') !== -1) return 'vendor-pixi';
          if (id.indexOf('@xyflow/react') !== -1) return 'vendor-flow';
          if (id.indexOf('esptool-js') !== -1) return 'vendor-esptool';
          return undefined;
        }
      }
    }
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8791',
        changeOrigin: true
      }
    }
  }
});
