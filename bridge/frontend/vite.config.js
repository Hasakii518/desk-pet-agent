import { defineConfig } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig({
  plugins: [svelte()],
  server: {
    port: 5173,
    // 开发期前端访问 5173，API/WS 代理到 agent :7777
    proxy: {
      '/api': 'http://127.0.0.1:7777',
      '/ws': { target: 'ws://127.0.0.1:7777', ws: true },
    },
  },
  build: {
    outDir: '../web/dist',
    sourcemap: false,
    emptyOutDir: true,
  },
})
