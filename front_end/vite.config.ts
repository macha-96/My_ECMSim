import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vitest/config'

// 开发代理：/api 与 /auth 转发到 C++ 服务器（协议不变）；/ws 走 WebSocket 升级
export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
      '/auth': 'http://localhost:8080',
      '/ws': { target: 'ws://localhost:8080', ws: true },
    },
  },
  test: {
    environment: 'jsdom',
    include: ['src/**/*.spec.ts'],
  },
})
