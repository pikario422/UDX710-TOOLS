<script setup>
import { computed, defineAsyncComponent, onMounted, onUnmounted, provide, ref } from 'vue'
import GlobalConfirm from './components/GlobalConfirm.vue'
import GlobalToast from './components/GlobalToast.vue'
import { authGetStatus, authLogin, clearAuthToken, fetchSystemInfo, getCurrentBand } from './composables/useApi'

const FeaturePage = defineAsyncComponent(() => import('./components/FeaturePage.vue'))

const groups = [
  {
    label: '工作台',
    items: [
      { id: 'overview', label: '设备概览', icon: 'gauge-high', description: '设备状态、蜂窝信号与资源使用情况' },
      { id: 'monitor', label: '设备监控', icon: 'heartbeat', description: '查看硬件、模组和运行状态详情' }
    ]
  },
  {
    label: '连接管理',
    items: [
      { id: 'network', label: '蜂窝连接', icon: 'tower-cell', description: '网络模式、SIM 卡、数据连接和漫游' },
      { id: 'netif', label: '网络接口', icon: 'ethernet', description: '查看接口状态、地址和实时速率' },
      { id: 'apn', label: 'APN 配置', icon: 'mobile-alt', description: '管理蜂窝网络接入点配置' },
      { id: 'advanced', label: '高级网络', icon: 'star', description: '频段、小区扫描与锁定策略' },
      { id: 'cells', label: '小区扫描', icon: 'satellite-dish', description: '扫描周边小区并锁定目标小区' }
    ]
  },
  {
    label: '通信与用量',
    items: [
      { id: 'sms', label: '短信', icon: 'envelope', description: '收发短信与短信转发设置' },
      { id: 'traffic', label: '流量统计', icon: 'chart-line', description: '流量用量、限额与周期策略' },
    ]
  },
  {
    label: '服务',
    items: [
      { id: 'rathole', label: 'Rathole 穿透', icon: 'exchange-alt', description: '配置内网穿透客户端服务' },
      { id: 'ipv6', label: 'IPv6 代理', icon: 'globe', description: '管理 IPv6 代理服务和端口' },
      { id: 'update', label: '系统更新', icon: 'cloud-download-alt', description: '检查并安装系统更新包' }
    ]
  },
  {
    label: '设备与维护',
    items: [
      { id: 'settings', label: '系统设置', icon: 'sliders', description: '设备、模组适配、时间与安全设置' },
      { id: 'at', label: 'AT 调试', icon: 'terminal', description: '向当前模组发送 AT 指令' },
      { id: 'terminal', label: 'Web 终端', icon: 'terminal', description: '打开设备维护终端' },
      { id: 'usb', label: 'USB 模式', icon: ['fab', 'usb'], description: '查看和切换 USB Gadget 模式' },
      { id: 'plugins', label: '插件管理', icon: 'puzzle-piece', description: '安装、启用和维护扩展插件' }
    ]
  }
]

const palettes = [
  { id: 'teal', label: '青绿', accent: '#0f766e', strong: '#0b5f59', soft: '#ecfdf8', border: '#c7e8e1', ink: '#134e4a', muted: '#6b8f8a', swatch: '#0f766e' },
  { id: 'blue', label: '蔚蓝', accent: '#2563eb', strong: '#1d4ed8', soft: '#eff6ff', border: '#bfdbfe', ink: '#1e3a8a', muted: '#6475a5', swatch: '#2563eb' },
  { id: 'rose', label: '玫瑰', accent: '#be385e', strong: '#9f2548', soft: '#fff1f4', border: '#fecdd6', ink: '#821b38', muted: '#9e6775', swatch: '#be385e' },
  { id: 'orange', label: '橙色', accent: '#c76a12', strong: '#a9540b', soft: '#fff7ed', border: '#fed7aa', ink: '#8b3e09', muted: '#987052', swatch: '#c76a12' }
]

const activeNav = ref('overview')
const sidebarOpen = ref(false)
const isDark = ref(localStorage.getItem('theme') === 'dark')
const paletteOpen = ref(false)
const paletteId = ref(localStorage.getItem('ui_palette') || 'teal')
const authChecking = ref(true)
const authenticated = ref(false)
const password = ref('')
const loginError = ref('')
const loginLoading = ref(false)
const showPassword = ref(false)
const systemInfo = ref({})
const currentBand = ref({})
const loading = ref(false)
const lastUpdated = ref('')
let refreshTimer

provide('systemInfo', systemInfo)
provide('loading', loading)
provide('handleLogout', logout)
provide('isDark', isDark)

const activePage = computed(() => groups.flatMap(group => group.items).find(item => item.id === activeNav.value))
const signal = computed(() => Number(currentBand.value.rsrp || systemInfo.value.rsrp || systemInfo.value.signal_strength || 0))
const signalLevel = computed(() => signal.value >= -80 ? 4 : signal.value >= -90 ? 3 : signal.value >= -105 ? 2 : signal.value ? 1 : 0)
const networkType = computed(() => currentBand.value.network_type || systemInfo.value.network_type || '未连接')

function applyTheme() {
  document.documentElement.classList.toggle('dark', isDark.value)
  localStorage.setItem('theme', isDark.value ? 'dark' : 'light')
}

function toggleTheme() {
  isDark.value = !isDark.value
  applyTheme()
}

function applyPalette() {
  const current = palettes.find(palette => palette.id === paletteId.value) || palettes[0]
  const root = document.documentElement.style
  root.setProperty('--accent', current.accent)
  root.setProperty('--accent-strong', current.strong)
  root.setProperty('--accent-soft', current.soft)
  root.setProperty('--accent-border', current.border)
  root.setProperty('--accent-ink', current.ink)
  root.setProperty('--accent-muted', current.muted)
}

function selectPalette(id) {
  paletteId.value = id
  localStorage.setItem('ui_palette', id)
  applyPalette()
  paletteOpen.value = false
}

function selectNav(id) {
  activeNav.value = id
  sidebarOpen.value = false
  paletteOpen.value = false
}

function value(input, fallback = '--') {
  return input === undefined || input === null || input === '' ? fallback : input
}

function uptime(seconds) {
  const total = Number(seconds || 0)
  if (!total) return '--'
  const days = Math.floor(total / 86400)
  const hours = Math.floor((total % 86400) / 3600)
  const minutes = Math.floor((total % 3600) / 60)
  return days ? `${days} 天 ${hours} 小时` : `${hours} 小时 ${minutes} 分钟`
}

function normalizeBand(payload) {
  if (payload?.Data) return payload.Data
  if (payload?.data && !Array.isArray(payload.data)) return payload.data
  return payload || {}
}

async function refresh() {
  if (!authenticated.value || loading.value) return
  loading.value = true
  try {
    const [info, band] = await Promise.all([fetchSystemInfo(), getCurrentBand()])
    systemInfo.value = info || {}
    currentBand.value = normalizeBand(band)
    lastUpdated.value = new Date().toLocaleTimeString()
  } catch (error) {
    console.warn('Dashboard refresh failed:', error.message)
  } finally {
    loading.value = false
  }
}

async function checkAuth() {
  try {
    if (!localStorage.getItem('auth_token')) return
    authenticated.value = (await authGetStatus())?.logged_in === true
    if (authenticated.value) {
      await refresh()
      refreshTimer = window.setInterval(refresh, 30000)
    }
  } catch {
    clearAuthToken()
    authenticated.value = false
  } finally {
    authChecking.value = false
  }
}

async function login() {
  if (!password.value || loginLoading.value) return
  loginLoading.value = true
  loginError.value = ''
  try {
    const result = await authLogin(password.value)
    if (!result.ok || !result.data?.token) throw new Error(result.data?.message || '密码错误')
    authenticated.value = true
    await refresh()
    refreshTimer = window.setInterval(refresh, 30000)
  } catch (error) {
    loginError.value = error.message || '登录失败，请检查后端服务'
  } finally {
    loginLoading.value = false
  }
}

function logout() {
  clearAuthToken()
  authenticated.value = false
  password.value = ''
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = undefined
  }
}

function onAuthRequired() {
  logout()
  authChecking.value = false
}

onMounted(() => {
  applyTheme()
  applyPalette()
  window.addEventListener('auth-required', onAuthRequired)
  checkAuth()
})

onUnmounted(() => {
  if (refreshTimer) clearInterval(refreshTimer)
  window.removeEventListener('auth-required', onAuthRequired)
})
</script>

<template>
  <div v-if="authChecking" class="boot-screen">
    <span class="loader"></span>
    <span>正在连接设备</span>
  </div>

  <div v-else-if="!authenticated" class="login-screen">
    <form class="login-panel" @submit.prevent="login">
      <div class="login-mark"><font-awesome-icon icon="tower-broadcast" /></div>
      <div class="eyebrow">UDX710 TOOLS</div>
      <h1>登录设备控制台</h1>
      <p>输入管理员密码以继续管理设备。</p>
      <label>
        管理员密码
        <span class="input-action">
          <input v-model="password" :type="showPassword ? 'text' : 'password'" autocomplete="current-password" autofocus placeholder="请输入密码">
          <button type="button" title="显示密码" @click="showPassword = !showPassword"><font-awesome-icon :icon="showPassword ? 'eye-slash' : 'eye'" /></button>
        </span>
      </label>
      <p v-if="loginError" class="form-error">{{ loginError }}</p>
      <button class="primary-button login-submit" :disabled="loginLoading || !password">
        <font-awesome-icon v-if="loginLoading" icon="spinner" spin />{{ loginLoading ? '登录中...' : '登录' }}
      </button>
      <div class="login-meta"><span class="status-dot"></span>等待设备认证</div>
    </form>
  </div>

  <div v-else class="app-frame">
    <div v-if="sidebarOpen" class="mobile-scrim" @click="sidebarOpen = false"></div>
    <aside class="sidebar" :class="{ open: sidebarOpen }">
      <div class="brand">
        <div class="brand-mark"><font-awesome-icon icon="tower-broadcast" /></div>
        <div><strong>UDX710 Tools</strong><span>设备控制台</span></div>
      </div>
      <nav class="sidebar-nav">
        <div v-for="group in groups" :key="group.label" class="nav-group">
          <div class="nav-group-title">{{ group.label }}</div>
          <button v-for="item in group.items" :key="item.id" class="nav-link" :class="{ active: activeNav === item.id }" @click="selectNav(item.id)">
            <font-awesome-icon :icon="item.icon" /><span>{{ item.label }}</span>
          </button>
        </div>
      </nav>
      <div class="sidebar-bottom">
        <div class="device-state"><span class="status-dot"></span><div><strong>设备在线</strong><small>oFono 服务正常</small></div></div>
        <button class="icon-button" title="退出登录" @click="logout"><font-awesome-icon icon="sign-out-alt" /></button>
      </div>
    </aside>

    <section class="workspace">
      <header class="topbar">
        <div class="topbar-title">
          <button class="icon-button mobile-menu" title="打开导航" @click="sidebarOpen = true"><font-awesome-icon icon="bars" /></button>
          <div><div class="breadcrumbs">设备控制台 / {{ activePage?.label }}</div><h1>{{ activePage?.label }}</h1></div>
        </div>
        <div class="topbar-actions">
          <div class="online-state"><span class="status-dot"></span>在线</div>
          <span class="updated">{{ lastUpdated ? `同步于 ${lastUpdated}` : '尚未同步' }}</span>
          <button class="icon-button" title="刷新概览数据" :disabled="loading" @click="refresh"><font-awesome-icon icon="sync-alt" :class="{ 'fa-spin': loading }" /></button>
          <button class="icon-button" title="切换明暗主题" @click="toggleTheme"><font-awesome-icon :icon="isDark ? 'sun' : 'moon'" /></button>
          <div class="palette-wrap">
            <button class="icon-button" title="主题配色" @click="paletteOpen = !paletteOpen"><font-awesome-icon icon="sliders" /></button>
            <div v-if="paletteOpen" class="palette-popover">
              <strong>主题颜色</strong><p>选择主操作色</p>
              <button v-for="palette in palettes" :key="palette.id" class="palette-option" :class="{ selected: paletteId === palette.id }" @click="selectPalette(palette.id)">
                <i :style="{ background: palette.swatch }"></i><span>{{ palette.label }}</span><font-awesome-icon v-if="paletteId === palette.id" icon="check" />
              </button>
            </div>
          </div>
          <button class="avatar-button" title="退出登录" @click="logout">A</button>
        </div>
      </header>

      <main class="page-content">
        <template v-if="activeNav === 'overview'">
          <div class="page-heading">
            <div><div class="eyebrow">实时状态</div><h2>设备概览</h2><p>查看蜂窝连接和设备资源的当前状态。</p></div>
            <button class="secondary-button" @click="refresh"><font-awesome-icon icon="sync-alt" />刷新状态</button>
          </div>
          <section class="overview-hero">
            <div class="hero-copy"><span class="section-kicker">当前连接</span><h3>{{ value(systemInfo.operator || systemInfo.carrier, '未识别运营商') }}</h3><div class="hero-tags"><span>{{ networkType }}</span><span>{{ value(currentBand.band, '频段未知') }}</span><span>{{ value(systemInfo.ip, 'IP 未分配') }}</span></div></div>
            <div class="hero-signal"><div class="signal-bars"><i v-for="level in 4" :key="level" :class="{ on: level <= signalLevel }"></i></div><div><strong>{{ signal ? `${signal} dBm` : '暂无信号' }}</strong><small>RSRP 信号强度</small></div></div>
          </section>
          <section class="metric-grid">
            <div class="metric"><div class="metric-label"><font-awesome-icon icon="microchip" /> CPU 使用率</div><strong>{{ value(systemInfo.cpu_usage, '0') }}<small>%</small></strong><div class="meter"><span :style="{ width: `${Math.min(Number(systemInfo.cpu_usage || 0), 100)}%` }"></span></div></div>
            <div class="metric"><div class="metric-label"><font-awesome-icon icon="memory" /> 内存使用率</div><strong>{{ value(systemInfo.memory_usage, '0') }}<small>%</small></strong><div class="meter blue"><span :style="{ width: `${Math.min(Number(systemInfo.memory_usage || 0), 100)}%` }"></span></div></div>
            <div class="metric"><div class="metric-label"><font-awesome-icon icon="temperature-half" /> 设备温度</div><strong>{{ value(systemInfo.thermal_temp || systemInfo.temperature) }}<small>°C</small></strong><span class="metric-note">运行正常</span></div>
            <div class="metric"><div class="metric-label"><font-awesome-icon icon="clock" /> 运行时间</div><strong>{{ uptime(systemInfo.uptime) }}</strong><span class="metric-note">自上次启动</span></div>
          </section>
          <div class="content-columns">
            <section class="panel"><div class="panel-header"><div><h3>网络详情</h3><p>当前蜂窝连接参数</p></div><button class="text-button" @click="selectNav('network')">管理连接 <font-awesome-icon icon="arrow-right" /></button></div><div class="detail-list"><div><span>网络制式</span><strong>{{ networkType }}</strong></div><div><span>频段</span><strong>{{ value(currentBand.band) }}</strong></div><div><span>频点 ARFCN</span><strong>{{ value(currentBand.arfcn) }}</strong></div><div><span>小区 PCI</span><strong>{{ value(currentBand.pci) }}</strong></div><div><span>RSRQ</span><strong>{{ currentBand.rsrq ? `${currentBand.rsrq} dB` : '--' }}</strong></div><div><span>SINR</span><strong>{{ currentBand.sinr ? `${currentBand.sinr} dB` : '--' }}</strong></div></div></section>
            <section class="panel"><div class="panel-header"><div><h3>快捷操作</h3><p>常用设备控制入口</p></div></div><div class="quick-actions"><button @click="selectNav('advanced')"><span class="action-icon green"><font-awesome-icon icon="tower-cell" /></span><span><strong>高级网络</strong><small>频段、小区和网络策略</small></span><font-awesome-icon icon="arrow-right" /></button><button @click="selectNav('sms')"><span class="action-icon blue"><font-awesome-icon icon="envelope" /></span><span><strong>短信管理</strong><small>收发短信和转发规则</small></span><font-awesome-icon icon="arrow-right" /></button><button @click="selectNav('settings')"><span class="action-icon orange"><font-awesome-icon icon="sliders" /></span><span><strong>系统设置</strong><small>设备与模组适配配置</small></span><font-awesome-icon icon="arrow-right" /></button></div></section>
          </div>
        </template>

        <template v-else>
          <div class="page-heading feature-heading"><div><div class="eyebrow">{{ activePage?.label }}</div><h2>{{ activePage?.label }}</h2><p>{{ activePage?.description }}</p></div></div>
          <FeaturePage :page="activeNav" />
        </template>
      </main>
    </section>
  </div>

  <GlobalToast />
  <GlobalConfirm />
</template>
