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
            imei: '867530900000000', iccid: '8986012380000000000', ip: '192.168.0.1', version: '1.2.0-preview',
            cpu_usage: 18, memory_usage: 42, uptime: 318420, sim_slot: 'slot1',
            select_network_mode: 'NR 5G/LTE auto'
          }
        }
        else if (path === '/current_band') body = { Code: 0, Data: { network_type: '5G NSA', band: 'N78', arfcn: 635334, pci: 301, rsrp: -91, rsrq: -11, sinr: 18 } }
        else if (path === '/data') body = { status: 'ok', data: { active: true } }
        else if (path === '/roaming') body = { status: 'ok', data: { roaming_allowed: true, is_roaming: false } }
        else if (path === '/bands') body = { Code: 0, Data: { '4G_TDD': ['B34', 'B38', 'B39', 'B40', 'B41'], '4G_FDD': ['B1', 'B3', 'B5', 'B8'], '5G': ['N1', 'N28', 'N41', 'N78'] } }
        else if (path === '/cells') body = { Code: 0, Data: [
          { isServing: true, band: 'N78', pci: 301, arfcn: 635334, rat: 'NR', rsrp: -91, rsrq: -11, sinr: 18 },
          { isServing: false, band: 'N78', pci: 198, arfcn: 636000, rat: 'NR', rsrp: -98, rsrq: -13, sinr: 12 }
        ] }
        else if (path === '/netif/list') body = { interfaces: [
          { name: 'rmnet_data0', type: 'cellular', status: 'up', monitoring: true, ipv4: '10.32.15.18', ipv6: '2409:8a1c:5a4:1100::2', gateway: '10.32.15.1', mac: '-' },
          { name: 'br-lan', type: 'bridge', status: 'up', monitoring: false, ipv4: '192.168.0.1', ipv6: '-', gateway: '-', mac: '02:00:00:00:00:01' }
        ] }
        else if (path === '/netif/stats') body = { rx_bytes: 1937462840, tx_bytes: 182746280, rx_rate: 29420, tx_rate: 6810 }
        else if (path === '/apn/config') body = { apn: 'cmnet', username: '', password: '', auth_type: 'none', pdp_type: 'IPV4V6' }
        else if (path === '/apn/templates') body = [{ id: 1, name: '中国移动 5G', apn: 'cmnet', protocol: 'dual', auth_method: 'none' }, { id: 2, name: '中国联通', apn: '3gnet', protocol: 'dual', auth_method: 'none' }]
        else if (path === '/get/Total') body = { rx: 1937462840, tx: 182746280, total: 2120209120 }
        else if (path === '/get/set') body = { switch: 1, much: 10737418240 }
        else if (path === '/usb/mode') body = { Code: 0, Data: { mode_value: 3, is_temporary: false } }
        else if (path === '/sms') body = [{ id: 1, sender: '10086', content: '欢迎使用 UDX710 预览设备', timestamp: '2026-08-04 10:30', read: false }, { id: 2, sender: '13800138000', content: '设备已连接到 5G 网络', timestamp: '2026-08-04 09:18', read: true }]
        else if (path === '/sms/sent') body = [{ id: 1, recipient: '13800138000', content: '测试短信', timestamp: '2026-08-04 09:15', status: 'sent' }]
        else if (path === '/sms/config') body = { auto_refresh: true, refresh_interval: 15, cnmi_enabled: true }
        else if (path === '/sms/webhook') body = { enabled: true, url: 'https://notify.example.test/sms', method: 'POST', body: '{"sender":"#{sender}","content":"#{content}"}', headers: 'Content-Type: application/json' }
        else if (path === '/sms/email') body = { enabled: true, smtp_host: 'smtp.example.com', smtp_port: 587, username: 'router@example.com', recipients: 'admin@example.com', use_tls: true }
        else if (path === '/sms/fix') body = { status: 'success', enabled: true, command: 'AT+CNMI=2,1,0,0,0' }
        else if (path === '/rathole/config') body = { server_addr: 'edge.example.test:2333', token: 'preview-token', enabled: 1, auto_start: 1 }
        else if (path === '/rathole/services') body = [{ id: 1, name: 'web-console', local_addr: '127.0.0.1:80', enabled: 1 }, { id: 2, name: 'ssh', local_addr: '127.0.0.1:22', enabled: 0 }]
        else if (path === '/rathole/status') body = { running: true, pid: 1842, service_count: 1 }
        else if (path === '/rathole/logs') body = { logs: '[INFO] preview tunnel connected\n[INFO] web-console is ready' }
        else if (path === '/ipv6-proxy/config') body = { enabled: true, listen_addr: '::', listen_port: 1080 }
        else if (path === '/ipv6-proxy/rules') body = [{ id: 1, local_port: 8080, ipv6_port: 18080, enabled: 1 }, { id: 2, local_port: 22, ipv6_port: 10022, enabled: 0 }]
        else if (path === '/ipv6-proxy/status') body = { running: true, connections: 3 }
        else if (path === '/ipv6-proxy/send-logs') body = { logs: [] }
        else if (path === '/update/version') body = { version: '1.2.0-preview' }
        else if (path === '/update/check') body = { has_update: true, current_version: '1.2.0-preview', latest_version: '1.3.0-preview', url: 'https://updates.example.test/udx710-1.3.0.tar.gz' }
        else if (path === '/modem-profile') body = { name: 'FM650', imei_set: '', imei_query: 'AT+CGSN', sms_cnmi_enabled: 'AT+CNMI=2,1,0,0,0', sms_cnmi_disabled: 'AT+CNMI=0,0,0,0,0', band_query_lte: 'AT+FBAND?', band_query_nr: 'AT+FNRBAND?', radio_off: 'AT+CFUN=0', radio_on: 'AT+CFUN=1' }
        else if (path === '/get/time') body = { time: '2026-08-04 14:30:00' }
        else if (path === '/plugins') body = { Code: 0, Data: [{ filename: 'sms-notify.js', name: 'sms-notify', content: 'module.exports = async function (message) { return message }', description: '短信通知示例' }] }
        else if (path === '/scripts') body = { Code: 0, Data: [{ name: 'startup.sh', content: '#!/bin/sh\necho UDX710 ready', description: '开机检查脚本' }, { name: 'network-check.sh', content: '#!/bin/sh\nroute -n', description: '网络诊断脚本' }] }

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
