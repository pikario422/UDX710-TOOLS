import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

function previewApiPlugin(enabled) {
  return {
    name: 'preview-api',
    configureServer(server) {
      if (!enabled) return
      server.middlewares.use('/api', (req, res) => {
        const path = new URL(req.url || '/', 'http://localhost').pathname
        let body = { status: 'success', data: [] }

        if (path === '/auth/login') body = { token: 'preview-token' }
        else if (path === '/auth/status') body = { logged_in: true }
        else if (path === '/security/status') body = { status: 'ok', data: { is_set: true } }
        else if (path === '/info') {
          body = {
            model: 'UDX710 Preview', network_type: '5G NSA', operator: 'Preview Carrier',
            signal: -87, rsrp: -91, rsrq: -11, sinr: 18, temperature: 42,
            battery: 78, imei: '867530900000000', ip: '192.168.0.1'
          }
        }

        res.statusCode = 200
        res.setHeader('Content-Type', 'application/json; charset=utf-8')
        res.end(JSON.stringify(body))
      })
    }
  }
}

export default defineConfig(({ mode }) => {
  const previewMode = mode === 'preview'
  return {
    plugins: [vue(), previewApiPlugin(previewMode)],
    base: './',
    resolve: {
      alias: {
        vue: 'vue/dist/vue.esm-bundler.js'
      }
    },
    build: {
      outDir: 'dist',
      assetsDir: 'assets',
      emptyOutDir: true,
      minify: 'esbuild',
      rollupOptions: {
        output: {
          manualChunks: undefined
        }
      }
    },
    server: previewMode ? {} : {
      proxy: {
        '/api': {
          target: 'http://192.168.0.1:80',
          changeOrigin: true
        }
      }
    }
  }
})
