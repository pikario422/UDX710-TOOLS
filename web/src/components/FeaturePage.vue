<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { authFetch } from '../composables/useApi'

const props = defineProps({ page: { type: String, required: true } })

const busy = ref(false)
const notice = ref('')
const system = ref({})
const network = ref({ airplane: false, mode: 'nr_5g_lte_auto', slot: 'slot1', data: false, roaming: false, isRoaming: false })
const interfaces = ref([])
const interfaceStats = ref({})
const apn = ref({ apn: '', username: '', password: '', auth_type: 'none', pdp_type: 'IPV4V6' })
const apnTemplates = ref([])
const bands = ref({ '4G_TDD': [], '4G_FDD': [], '5G': [] })
const selectedBands = ref([])
const cells = ref([])
const selectedCell = ref(null)
const messages = ref([])
const sentMessages = ref([])
const smsForm = ref({ recipient: '', content: '' })
const smsConfig = ref({ auto_refresh: true, refresh_interval: 15 })
const emailConfig = ref({ enabled: false, smtp_host: '', smtp_port: 587, username: '', password: '', recipients: '', use_tls: true })
const webhookConfig = ref({ enabled: false, url: '', method: 'POST', body: '', headers: '' })
const traffic = ref({ rx: 0, tx: 0, total: 0, limitEnabled: false, limitGb: 10 })
const rathole = ref({ config: { server_addr: '', auto_start: 0, enabled: 0 }, status: {}, services: [] })
const ipv6 = ref({ config: { enabled: false, listen_addr: '::', listen_port: 1080 }, status: {}, rules: [] })
const update = ref({ version: '', available: null, latest: '', url: '' })
const updateFile = ref(null)
const modemProfile = ref({ name: '', imei_query: 'AT+CGSN', imei_set: '' })
const systemTime = ref('')
const passwordForm = ref({ old_password: '', new_password: '', confirm: '' })
const at = ref({ command: '', output: '', history: [] })
const usb = ref({ mode_value: null, is_temporary: false })
const plugins = ref([])
const scripts = ref([])
const pluginForm = ref({ name: '', content: '' })
const scriptForm = ref({ name: '', content: '' })
const editingScript = ref(null)
const terminalUrl = computed(() => `http://${window.location.hostname || 'localhost'}:7681`)

function feedback(message, isError = false) {
  notice.value = message
  window.setTimeout(() => { if (notice.value === message) notice.value = '' }, isError ? 5000 : 3000)
}

function unwrap(payload) {
  if (payload?.Data !== undefined) return payload.Data
  if (payload?.data !== undefined) return payload.data
  return payload
}

function passed(payload) {
  return !payload || payload.Code === undefined || payload.Code === 0 || payload.status === undefined || payload.status === 'ok' || payload.status === 'success'
}

async function api(path, options = {}) {
  const response = await authFetch(`/api${path}`, options)
  const data = await response.json()
  if (!response.ok || !passed(data)) throw new Error(data?.Error || data?.error || `HTTP ${response.status}`)
  return data
}

async function load() {
  busy.value = true
  try {
    if (props.page === 'monitor') system.value = await api('/info')
    if (props.page === 'network') await loadNetwork()
    if (props.page === 'netif') interfaces.value = (await api('/netif/list')).interfaces || []
    if (props.page === 'apn') await loadApn()
    if (props.page === 'advanced' || props.page === 'cells') await loadRadio()
    if (props.page === 'sms') await loadSms()
    if (props.page === 'traffic') await loadTraffic()
    if (props.page === 'rathole') await loadRathole()
    if (props.page === 'ipv6') await loadIpv6()
    if (props.page === 'update') await loadUpdate()
    if (props.page === 'settings') await loadSettings()
    if (props.page === 'usb') usb.value = unwrap(await api('/usb/mode')) || {}
    if (props.page === 'plugins') await loadPlugins()
  } catch (error) {
    feedback(`加载失败：${error.message}`, true)
  } finally {
    busy.value = false
  }
}

async function loadNetwork() {
  const [info, data, roaming] = await Promise.all([api('/info'), api('/data'), api('/roaming')])
  system.value = info || {}
  network.value = {
    airplane: info.airplane_mode === true || info.airplane_mode === 1,
    mode: modeValue(info.select_network_mode), slot: info.sim_slot || 'slot1',
    data: Boolean(unwrap(data)?.active), roaming: Boolean(unwrap(roaming)?.roaming_allowed), isRoaming: Boolean(unwrap(roaming)?.is_roaming)
  }
}

function modeValue(value = '') {
  if (value.includes('LTE only')) return 'lte_only'
  if (value.includes('NR 5G only')) return 'nr_5g_only'
  if (value.includes('NSA only')) return 'nsa_only'
  return 'nr_5g_lte_auto'
}

async function loadApn() {
  const [config, templates] = await Promise.all([api('/apn/config'), api('/apn/templates')])
  apn.value = { ...apn.value, ...(unwrap(config) || {}) }
  apnTemplates.value = unwrap(templates) || []
}

async function loadRadio() {
  const [bandData, cellData] = await Promise.all([api('/bands'), api('/cells')])
  bands.value = unwrap(bandData) || bands.value
  cells.value = unwrap(cellData) || []
}

async function loadSms() {
  const [inbox, sent, config, email, webhook] = await Promise.all([api('/sms'), api('/sms/sent'), api('/sms/config'), api('/sms/email'), api('/sms/webhook')])
  messages.value = Array.isArray(inbox) ? inbox : unwrap(inbox) || []
  sentMessages.value = Array.isArray(sent) ? sent : unwrap(sent) || []
  smsConfig.value = { ...smsConfig.value, ...(unwrap(config) || {}) }
  emailConfig.value = { ...emailConfig.value, ...(unwrap(email) || {}), password: '' }
  webhookConfig.value = { ...webhookConfig.value, ...(unwrap(webhook) || {}) }
}

async function loadTraffic() {
  const [total, config] = await Promise.all([api('/get/Total'), api('/get/set')])
  const settings = unwrap(config) || {}
  traffic.value = { rx: Number(total.rx || 0), tx: Number(total.tx || 0), total: Number(total.total || 0), limitEnabled: settings.switch === 1, limitGb: Number(settings.much || 0) / 1073741824 || 10 }
}

async function loadRathole() {
  const [config, status, services] = await Promise.all([api('/rathole/config'), api('/rathole/status'), api('/rathole/services')])
  rathole.value = { config: unwrap(config) || {}, status: unwrap(status) || {}, services: unwrap(services) || [] }
}

async function loadIpv6() {
  const [config, status, rules] = await Promise.all([api('/ipv6-proxy/config'), api('/ipv6-proxy/status'), api('/ipv6-proxy/rules')])
  ipv6.value = { config: unwrap(config) || {}, status: unwrap(status) || {}, rules: unwrap(rules) || [] }
}

async function loadUpdate() {
  const [version, check] = await Promise.all([api('/update/version'), api('/update/check')])
  update.value = { version: unwrap(version)?.version || '', available: unwrap(check)?.has_update, latest: unwrap(check)?.latest_version || '', url: unwrap(check)?.url || '' }
}

async function loadSettings() {
  const [profile, time] = await Promise.all([api('/modem-profile'), api('/get/time')])
  modemProfile.value = { ...modemProfile.value, ...(unwrap(profile) || {}) }
  systemTime.value = unwrap(time)?.time || unwrap(time)?.current_time || ''
}

async function loadPlugins() {
  const [pluginList, scriptList] = await Promise.all([api('/plugins'), api('/scripts')])
  plugins.value = unwrap(pluginList) || []
  scripts.value = unwrap(scriptList) || []
}

async function act(label, action) {
  busy.value = true
  try {
    await action()
    feedback(`${label}已完成`)
    await load()
  } catch (error) {
    feedback(`${label}失败：${error.message}`, true)
  } finally {
    busy.value = false
  }
}

function post(path, body = {}) { return api(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }) }
function put(path, body = {}) { return api(path, { method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }) }

function toggleBand(band) {
  selectedBands.value = selectedBands.value.includes(band) ? selectedBands.value.filter(item => item !== band) : [...selectedBands.value, band]
}

async function sendAt() {
  const command = at.value.command.trim()
  if (!command) return
  await act('AT 指令执行', async () => {
    const result = await post('/at', { command })
    const output = unwrap(result) || 'OK'
    at.value.output = typeof output === 'string' ? output : JSON.stringify(output, null, 2)
    at.value.history = [{ command, output: at.value.output, time: new Date().toLocaleTimeString() }, ...at.value.history].slice(0, 12)
  })
}

function selectUpdateFile(event) {
  updateFile.value = event.target.files?.[0] || null
  event.target.value = ''
}

async function installUpdate() {
  if (!updateFile.value && !update.available) return
  await act('系统更新安装', async () => {
    if (updateFile.value) {
      const formData = new FormData()
      formData.append('file', updateFile.value)
      const response = await authFetch('/api/update/upload', { method: 'POST', body: formData })
      const result = await response.json()
      if (!response.ok || result.error) throw new Error(result.error || `HTTP ${response.status}`)
    } else {
      await post('/update/download', { url: update.value.url })
    }
    await post('/update/extract')
    await post('/update/install')
    updateFile.value = null
  })
}

function bytes(value) {
  const amount = Number(value || 0)
  if (!amount) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const index = Math.min(Math.floor(Math.log(amount) / Math.log(1024)), units.length - 1)
  return `${(amount / 1024 ** index).toFixed(index ? 1 : 0)} ${units[index]}`
}

function modeName(value) { return { 1: 'CDC-NCM', 2: 'CDC-ECM', 3: 'RNDIS' }[value] || '未知' }

function downloadFile(filename, content, type = 'text/plain') {
  const url = URL.createObjectURL(new Blob([content], { type }))
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = filename
  anchor.click()
  URL.revokeObjectURL(url)
}

function exportPlugin(plugin) {
  downloadFile(plugin.filename || `${plugin.name || 'plugin'}.js`, plugin.content || '', 'application/javascript')
  feedback('插件已导出')
}

function exportPlugins() {
  downloadFile(`plugins_backup_${Date.now()}.json`, JSON.stringify({ version: '1.0', plugins: plugins.value.map(item => ({ filename: item.filename, content: item.content })) }, null, 2), 'application/json')
  feedback(`已导出 ${plugins.value.length} 个插件`)
}

async function importPlugins(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  try {
    const data = JSON.parse(await file.text())
    if (!Array.isArray(data.plugins)) throw new Error('文件格式无效')
    let count = 0
    for (const item of data.plugins) {
      if (!item.filename || !item.content) continue
      await post('/plugins', { name: item.filename.replace(/\.js$/, ''), content: item.content })
      count++
    }
    feedback(`已导入 ${count} 个插件`)
    await load()
  } catch (error) { feedback(`插件导入失败：${error.message}`, true) }
}

async function savePlugin() {
  if (!pluginForm.value.name.trim() || !pluginForm.value.content.trim()) return feedback('插件名称和内容不能为空', true)
  await act('插件保存', async () => {
    await post('/plugins', { name: pluginForm.value.name.trim().replace(/\.js$/, ''), content: pluginForm.value.content })
    pluginForm.value = { name: '', content: '' }
  })
}

async function removePlugin(plugin) {
  await act('插件删除', () => api(`/plugins/${encodeURIComponent(plugin.filename || plugin.name)}`, { method: 'DELETE' }))
}

async function clearPlugins() {
  await act('插件清空', () => api('/plugins/all', { method: 'DELETE' }))
}

function exportScript(script) {
  downloadFile(script.name || 'script.sh', script.content || '', 'text/x-shellscript')
  feedback('脚本已导出')
}

function exportScripts() {
  downloadFile(`scripts_backup_${Date.now()}.json`, JSON.stringify({ version: '1.0', type: 'scripts', scripts: scripts.value.map(item => ({ name: item.name, content: item.content })) }, null, 2), 'application/json')
  feedback(`已导出 ${scripts.value.length} 个脚本`)
}

async function importScripts(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  try {
    const text = await file.text()
    const items = file.name.endsWith('.json') ? (JSON.parse(text).scripts || []) : [{ name: file.name.endsWith('.sh') ? file.name : `${file.name}.sh`, content: text }]
    let count = 0
    for (const item of items) {
      if (!item.name || !item.content) continue
      await post('/scripts', { name: item.name.endsWith('.sh') ? item.name : `${item.name}.sh`, content: item.content })
      count++
    }
    feedback(`已导入 ${count} 个脚本`)
    await load()
  } catch (error) { feedback(`脚本导入失败：${error.message}`, true) }
}

async function saveScript() {
  if (!scriptForm.value.name.trim() || !scriptForm.value.content.trim()) return feedback('脚本名称和内容不能为空', true)
  const name = scriptForm.value.name.trim().endsWith('.sh') ? scriptForm.value.name.trim() : `${scriptForm.value.name.trim()}.sh`
  await act(editingScript.value ? '脚本更新' : '脚本保存', async () => {
    if (editingScript.value) await put(`/scripts/${encodeURIComponent(editingScript.value.name)}`, { content: scriptForm.value.content })
    else await post('/scripts', { name, content: scriptForm.value.content })
    scriptForm.value = { name: '', content: '' }
    editingScript.value = null
  })
}

function editScript(script) {
  editingScript.value = script
  scriptForm.value = { name: script.name || '', content: script.content || '' }
}

async function removeScript(script) {
  await act('脚本删除', () => api(`/scripts/${encodeURIComponent(script.name)}`, { method: 'DELETE' }))
}

watch(() => props.page, load)
onMounted(load)
onBeforeUnmount(() => { notice.value = '' })
</script>

<template>
  <div class="feature-page" :aria-busy="busy">
    <div v-if="notice" class="feature-notice">{{ notice }}</div>
    <div v-if="busy" class="feature-loading"><span class="loader"></span><span>正在同步设备数据</span></div>

    <template v-else-if="page === 'monitor'">
      <section class="feature-grid metrics-3"><article class="feature-card"><span>设备型号</span><strong>{{ system.model || system.model_name || '--' }}</strong></article><article class="feature-card"><span>IMEI</span><strong>{{ system.imei || '--' }}</strong></article><article class="feature-card"><span>ICCID</span><strong>{{ system.iccid || '--' }}</strong></article></section>
      <section class="feature-card"><div class="feature-card-header"><h3>模组状态</h3><button class="table-action" @click="load">刷新</button></div><dl class="data-grid"><div><dt>运营商</dt><dd>{{ system.operator || system.carrier || '--' }}</dd></div><div><dt>网络制式</dt><dd>{{ system.network_type || '--' }}</dd></div><div><dt>IP 地址</dt><dd>{{ system.ip || '--' }}</dd></div><div><dt>温度</dt><dd>{{ system.temperature || system.thermal_temp || '--' }} °C</dd></div><div><dt>软件版本</dt><dd>{{ system.version || '--' }}</dd></div></dl></section>
    </template>

    <template v-else-if="page === 'network'">
      <section class="feature-grid two-columns"><article class="feature-card"><div class="feature-card-header"><h3>网络模式</h3><span class="state-chip">{{ system.network_type || '未连接' }}</span></div><div class="segment-control"><button v-for="item in [{ id: 'nr_5g_lte_auto', label: '自动' }, { id: 'nr_5g_only', label: '5G' }, { id: 'lte_only', label: '4G' }, { id: 'nsa_only', label: 'NSA' }]" :key="item.id" :class="{ selected: network.mode === item.id }" @click="act('网络模式切换', () => post('/set_network', { mode: item.id }))">{{ item.label }}</button></div><dl class="data-grid compact"><div><dt>运营商</dt><dd>{{ system.operator || system.carrier || '--' }}</dd></div><div><dt>SIM 卡槽</dt><dd>{{ network.slot }}</dd></div></dl></article><article class="feature-card"><h3>连接开关</h3><div class="switch-list"><label><span>飞行模式</span><input v-model="network.airplane" type="checkbox" @change="act('飞行模式更新', () => post('/airplane_mode', { enabled: network.airplane }))"></label><label><span>蜂窝数据</span><input v-model="network.data" type="checkbox" @change="act('蜂窝数据更新', () => post('/data', { active: network.data }))"></label><label><span>允许数据漫游</span><input v-model="network.roaming" type="checkbox" @change="act('漫游设置更新', () => post('/roaming', { allowed: network.roaming }))"></label></div><div class="form-row"><button class="secondary-button" @click="act('SIM 卡槽切换', () => post('/switch', { slot: network.slot === 'slot1' ? 'slot2' : 'slot1' }))">切换 SIM 卡槽</button></div></article></section>
    </template>

    <template v-else-if="page === 'netif'">
      <section class="feature-card"><div class="feature-card-header"><h3>网络接口</h3><button class="table-action" @click="load">刷新</button></div><div class="data-table"><div class="table-row table-head"><span>接口</span><span>状态</span><span>IPv4</span><span>实时速率</span><span></span></div><div v-for="item in interfaces" :key="item.name" class="table-row"><strong>{{ item.name }}</strong><span><i class="state-dot" :class="{ muted: item.status !== 'up' }"></i>{{ item.status }}</span><span>{{ item.ipv4 || '--' }}</span><span>{{ bytes(interfaceStats[item.name]?.rx_rate || 0) }}/s</span><button class="table-action" @click="act('接口统计刷新', async () => { interfaceStats[item.name] = await api('/netif/stats', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ interface: item.name }) }) })">详情</button></div></div></section>
    </template>

    <template v-else-if="page === 'apn'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('APN 配置保存', () => post('/apn/config', apn))"><div class="feature-card-header"><h3>当前 APN</h3><button class="primary-button">保存</button></div><label>接入点名称<input v-model="apn.apn" placeholder="例如 cmnet"></label><label>用户名<input v-model="apn.username"></label><label>密码<input v-model="apn.password" type="password"></label><label>认证方式<select v-model="apn.auth_type"><option value="none">无认证</option><option value="pap">PAP</option><option value="chap">CHAP</option></select></label><label>PDP 类型<select v-model="apn.pdp_type"><option>IPV4V6</option><option>IPV4</option><option>IPV6</option></select></label></form><section class="feature-card"><div class="feature-card-header"><h3>预设模板</h3><span>{{ apnTemplates.length }} 个</span></div><div class="simple-list"><button v-for="item in apnTemplates" :key="item.id || item.name" @click="apn = { ...apn, ...item }"><span><strong>{{ item.name || item.apn }}</strong><small>{{ item.apn }}</small></span><font-awesome-icon icon="arrow-right" /></button><p v-if="!apnTemplates.length" class="empty-copy">暂无保存的 APN 模板</p></div></section></section>
    </template>

    <template v-else-if="page === 'advanced' || page === 'cells'">
      <template v-if="page === 'advanced'"><section class="feature-card"><div class="feature-card-header"><div><h3>频段策略</h3><p>选择频段后应用锁定策略。</p></div><div class="button-group"><button class="secondary-button" @click="act('频段解锁', () => post('/unlock_bands'))">全部解锁</button><button class="primary-button" :disabled="!selectedBands.length" @click="act('频段锁定', () => post('/lock_bands', { bands: selectedBands }))">应用锁定</button></div></div><div class="band-groups"><div v-for="(items, type) in bands" :key="type"><span>{{ type }}</span><div class="band-list"><button v-for="band in items" :key="band" :class="{ selected: selectedBands.includes(band) }" @click="toggleBand(band)">{{ band }}</button></div></div></div></section></template>
      <section class="feature-card"><div class="feature-card-header"><div><h3>{{ page === 'cells' ? '小区扫描' : '当前与周边小区' }}</h3><p>选择一个目标小区后可直接锁定。</p></div><div class="button-group"><button class="secondary-button" @click="act('小区解锁', () => post('/unlock_cell'))">解除锁定</button><button class="primary-button" :disabled="!selectedCell" @click="act('小区锁定', () => post('/lock_cell', { technology: selectedCell?.rat === 'NR' ? 'NR' : 'LTE', arfcn: String(selectedCell?.arfcn), pci: String(selectedCell?.pci) }))">锁定所选</button></div></div><div class="cell-list"><button v-for="cell in cells" :key="`${cell.rat}-${cell.arfcn}-${cell.pci}`" :class="{ selected: selectedCell?.pci === cell.pci && selectedCell?.arfcn === cell.arfcn }" @click="selectedCell = cell"><span class="cell-title"><strong>{{ cell.isServing ? '服务小区' : '邻区' }}</strong><small>{{ cell.rat }} · {{ cell.band }}</small></span><span>PCI {{ cell.pci }}</span><span>ARFCN {{ cell.arfcn }}</span><span>{{ cell.rsrp }} dBm</span></button></div></section>
    </template>

    <template v-else-if="page === 'sms'">
      <section class="feature-grid sms-layout"><section class="feature-card"><div class="feature-card-header"><h3>收件箱</h3><button class="table-action" @click="load">刷新</button></div><div class="message-list"><article v-for="message in messages" :key="message.id"><div><strong>{{ message.sender || message.phone || '未知号码' }}</strong><p>{{ message.content || message.body }}</p></div><time>{{ message.timestamp }}</time><button title="删除短信" @click="act('短信删除', () => api(`/sms/${message.id}`, { method: 'DELETE' }))"><font-awesome-icon icon="trash" /></button></article><p v-if="!messages.length" class="empty-copy">暂无短信</p></div></section><form class="feature-card form-grid" @submit.prevent="act('短信发送', () => post('/sms/send', smsForm))"><div class="feature-card-header"><h3>发送短信</h3><button class="primary-button">发送</button></div><label>收件号码<input v-model="smsForm.recipient" type="tel" placeholder="13800138000"></label><label>短信内容<textarea v-model="smsForm.content" rows="6" maxlength="500"></textarea></label></form></section>
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('短信转发设置保存', () => post('/sms/webhook', webhookConfig))"><div class="feature-card-header"><h3>Webhook 转发</h3><label class="inline-switch"><input v-model="webhookConfig.enabled" type="checkbox"><span>启用</span></label></div><label>地址<input v-model="webhookConfig.url" type="url" placeholder="https://"></label><label>方法<select v-model="webhookConfig.method"><option>POST</option><option>GET</option></select></label><label>请求内容<textarea v-model="webhookConfig.body" rows="3"></textarea></label><button class="secondary-button">保存转发设置</button></form><form class="feature-card form-grid" @submit.prevent="act('邮件转发设置保存', () => post('/sms/email', emailConfig))"><div class="feature-card-header"><h3>邮件转发</h3><label class="inline-switch"><input v-model="emailConfig.enabled" type="checkbox"><span>启用</span></label></div><label>SMTP 主机<input v-model="emailConfig.smtp_host"></label><label>端口<input v-model.number="emailConfig.smtp_port" type="number"></label><label>收件人<input v-model="emailConfig.recipients" placeholder="a@example.com,b@example.com"></label><button class="secondary-button">保存邮件设置</button></form></section>
    </template>

    <template v-else-if="page === 'traffic'">
      <section class="feature-grid metrics-3"><article class="feature-card"><span>下载用量</span><strong>{{ bytes(traffic.rx) }}</strong></article><article class="feature-card"><span>上传用量</span><strong>{{ bytes(traffic.tx) }}</strong></article><article class="feature-card"><span>累计流量</span><strong>{{ bytes(traffic.total) }}</strong></article></section><section class="feature-card"><form class="settings-row" @submit.prevent="act('流量限额保存', () => post('/set/total', { switch: traffic.limitEnabled ? 1 : 0, much: Math.round(traffic.limitGb * 1073741824) }))"><label class="inline-switch"><input v-model="traffic.limitEnabled" type="checkbox"><span>启用流量限额</span></label><label class="inline-input">限额 (GB)<input v-model.number="traffic.limitGb" :disabled="!traffic.limitEnabled" type="number" min="1"></label><div class="button-group"><button type="button" class="secondary-button" @click="act('流量统计清除', () => post('/set/total'))">清除统计</button><button class="primary-button">保存</button></div></form></section>
    </template>

    <template v-else-if="page === 'rathole'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('Rathole 配置保存', () => post('/rathole/config', rathole.config))"><div class="feature-card-header"><h3>客户端配置</h3><span class="state-chip" :class="{ inactive: !rathole.status.running }">{{ rathole.status.running ? '运行中' : '未运行' }}</span></div><label>服务器地址<input v-model="rathole.config.server_addr" placeholder="host:2333"></label><label class="inline-switch"><input v-model="rathole.config.enabled" true-value="1" false-value="0" type="checkbox"><span>启用服务</span></label><label class="inline-switch"><input v-model="rathole.config.auto_start" true-value="1" false-value="0" type="checkbox"><span>开机启动</span></label><div class="button-group"><button type="button" class="secondary-button" @click="act('服务启动', () => post('/rathole/start'))">启动</button><button type="button" class="secondary-button" @click="act('服务停止', () => post('/rathole/stop'))">停止</button><button class="primary-button">保存</button></div></form><section class="feature-card"><div class="feature-card-header"><h3>转发服务</h3><span>{{ rathole.services.length }} 个</span></div><div class="simple-list"><div v-for="service in rathole.services" :key="service.id || service.name"><span><strong>{{ service.name }}</strong><small>{{ service.local_addr }}</small></span><span class="state-chip" :class="{ inactive: !service.enabled }">{{ service.enabled ? '启用' : '停用' }}</span></div><p v-if="!rathole.services.length" class="empty-copy">暂无转发服务</p></div></section></section>
    </template>

    <template v-else-if="page === 'ipv6'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('IPv6 代理配置保存', () => post('/ipv6-proxy/config', ipv6.config))"><div class="feature-card-header"><h3>服务配置</h3><span class="state-chip" :class="{ inactive: !ipv6.status.running }">{{ ipv6.status.running ? '运行中' : '未运行' }}</span></div><label class="inline-switch"><input v-model="ipv6.config.enabled" type="checkbox"><span>启用 IPv6 代理</span></label><label>监听地址<input v-model="ipv6.config.listen_addr"></label><label>端口<input v-model.number="ipv6.config.listen_port" type="number"></label><div class="button-group"><button type="button" class="secondary-button" @click="act('代理启动', () => post('/ipv6-proxy/start'))">启动</button><button type="button" class="secondary-button" @click="act('代理停止', () => post('/ipv6-proxy/stop'))">停止</button><button class="primary-button">保存</button></div></form><section class="feature-card"><div class="feature-card-header"><h3>代理规则</h3><span>{{ ipv6.rules.length }} 条</span></div><div class="simple-list"><div v-for="rule in ipv6.rules" :key="rule.id"><span><strong>{{ rule.name || rule.domain }}</strong><small>{{ rule.target || rule.address }}</small></span></div><p v-if="!ipv6.rules.length" class="empty-copy">暂无代理规则</p></div></section></section>
    </template>

    <template v-else-if="page === 'update'">
      <section class="feature-grid two-columns"><article class="feature-card"><span>当前版本</span><strong>{{ update.version || '--' }}</strong><p class="card-copy">{{ update.available ? `发现新版本 ${update.latest}` : '当前已是最新版本' }}</p></article><section class="feature-card"><h3>在线更新</h3><p class="card-copy">从更新源下载、校验并安装系统包。</p><div class="button-group"><button class="secondary-button" @click="act('更新检查', () => api('/update/check'))">检查更新</button><button class="primary-button" :disabled="!update.available" @click="installUpdate">在线安装</button></div></section></section><section class="feature-card update-upload"><div><h3>本地安装包</h3><p>{{ updateFile ? updateFile.name : '尚未选择安装包' }}</p></div><div class="button-group"><label class="secondary-button file-button">选择安装包<input type="file" accept=".tar,.gz,.tgz,.zip,.bin" @change="selectUpdateFile"></label><button class="primary-button" :disabled="!updateFile" @click="installUpdate">上传并安装</button></div></section>
    </template>

    <template v-else-if="page === 'settings'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('模组配置保存', () => post('/modem-profile', modemProfile))"><div class="feature-card-header"><h3>模组适配配置</h3><button class="primary-button">保存</button></div><label>配置名称<input v-model="modemProfile.name"></label><label>IMEI 查询指令<input v-model="modemProfile.imei_query"></label><label>IMEI 写入模板<input v-model="modemProfile.imei_set" placeholder="留空则禁用"></label><div class="button-group"><button type="button" class="secondary-button" @click="act('模组配置重置', () => post('/modem-profile/reset'))">恢复默认</button></div></form><section class="feature-card"><h3>设备控制</h3><dl class="data-grid compact"><div><dt>系统时间</dt><dd>{{ systemTime || '--' }}</dd></div></dl><div class="button-group"><button class="secondary-button" @click="act('时间同步', () => post('/set/time'))">同步时间</button><button class="danger-button" @click="act('设备重启', () => post('/device_control', { action: 'reboot' }))">重启设备</button></div></section></section><form class="feature-card settings-row" @submit.prevent="act('密码修改', () => post('/auth/password', passwordForm))"><label class="inline-input">当前密码<input v-model="passwordForm.old_password" type="password"></label><label class="inline-input">新密码<input v-model="passwordForm.new_password" type="password"></label><label class="inline-input">确认新密码<input v-model="passwordForm.confirm" type="password"></label><button class="primary-button">修改密码</button></form>
    </template>

    <template v-else-if="page === 'at'">
      <section class="feature-grid at-layout"><section class="feature-card"><div class="feature-card-header"><h3>AT 指令</h3><button class="table-action" @click="at.history = []; at.output = ''">清空记录</button></div><div class="quick-command"><button v-for="command in ['AT+CGSN', 'AT+CCID', 'AT+CSQ', 'AT+COPS?']" :key="command" @click="at.command = command">{{ command }}</button></div><div class="command-row"><input v-model="at.command" placeholder="输入 AT 指令" @keyup.enter="sendAt"><button class="primary-button" @click="sendAt">执行</button></div><pre v-if="at.output" class="command-output">{{ at.output }}</pre></section><section class="feature-card"><h3>执行记录</h3><div class="history-list"><div v-for="item in at.history" :key="`${item.time}-${item.command}`"><strong>{{ item.command }}</strong><small>{{ item.time }}</small></div><p v-if="!at.history.length" class="empty-copy">暂无执行记录</p></div></section></section>
    </template>

    <template v-else-if="page === 'terminal'">
      <section class="terminal-panel"><div class="terminal-bar"><span><i></i><i></i><i></i></span><strong>设备终端</strong><a :href="terminalUrl" target="_blank" rel="noopener"><font-awesome-icon icon="external-link-alt" /> 新窗口</a></div><iframe :src="terminalUrl" title="设备终端" allow="clipboard-read; clipboard-write"></iframe></section>
    </template>

    <template v-else-if="page === 'usb'">
      <section class="feature-card"><div class="feature-card-header"><div><h3>当前 USB 模式</h3><p>{{ usb.is_temporary ? '临时模式，设备重启后恢复' : '永久模式' }}</p></div><strong class="mode-value">{{ modeName(usb.mode_value) }}</strong></div><div class="usb-mode-grid"><article v-for="mode in [{ id: 1, name: 'CDC-NCM' }, { id: 2, name: 'CDC-ECM' }, { id: 3, name: 'RNDIS' }]" :key="mode.id" :class="{ selected: usb.mode_value === mode.id }"><strong>{{ mode.name }}</strong><span>{{ mode.id === 1 ? '高性能网络接口' : mode.id === 2 ? '通用以太网接口' : 'Windows 兼容模式' }}</span><div class="button-group"><button class="secondary-button" @click="act('USB 热切换', () => post('/usb-advance', { mode: mode.id }))">临时切换</button><button class="primary-button" @click="act('USB 模式保存', () => post('/usb/mode', { mode: mode.id, permanent: true }))">永久切换</button></div></article></div></section>
    </template>

    <template v-else-if="page === 'plugins'">
      <section class="feature-grid two-columns">
        <section class="feature-card">
          <div class="feature-card-header"><div><h3>插件管理</h3><p>{{ plugins.length }} 个已安装插件</p></div><div class="button-group"><button class="table-action" @click="load">刷新</button><button class="table-action" @click="exportPlugins">导出全部</button><label class="table-action file-button">导入<input type="file" accept=".json,application/json" @change="importPlugins"></label></div></div>
          <form class="form-grid compact-form" @submit.prevent="savePlugin"><label>插件名称<input v-model="pluginForm.name" placeholder="example"></label><label>插件代码<textarea v-model="pluginForm.content" rows="5" placeholder="输入 JavaScript 插件内容"></textarea></label><div class="button-group"><button class="primary-button">保存插件</button><button type="button" class="danger-button" :disabled="!plugins.length" @click="clearPlugins">清空插件</button></div></form>
          <div class="simple-list"><div v-for="plugin in plugins" :key="plugin.filename || plugin.name"><span><strong>{{ plugin.filename || plugin.name }}</strong><small>{{ plugin.description || '本地扩展插件' }}</small></span><span class="button-group"><button class="table-action" @click="exportPlugin(plugin)">导出</button><button class="table-action danger-text" @click="removePlugin(plugin)">删除</button></span></div><p v-if="!plugins.length" class="empty-copy">暂无已安装插件</p></div>
        </section>
        <section class="feature-card">
          <div class="feature-card-header"><div><h3>脚本管理</h3><p>{{ scripts.length }} 个自动化脚本</p></div><div class="button-group"><button class="table-action" @click="exportScripts">导出全部</button><label class="table-action file-button">导入<input type="file" accept=".sh,.json,text/x-shellscript,application/json" @change="importScripts"></label></div></div>
          <form class="form-grid compact-form" @submit.prevent="saveScript"><label>脚本名称<input v-model="scriptForm.name" placeholder="startup.sh"></label><label>脚本内容<textarea v-model="scriptForm.content" rows="5" placeholder="#!/bin/sh"></textarea></label><div class="button-group"><button class="primary-button">{{ editingScript ? '保存修改' : '新建脚本' }}</button><button v-if="editingScript" type="button" class="secondary-button" @click="editingScript = null; scriptForm = { name: '', content: '' }">取消编辑</button></div></form>
          <div class="simple-list"><div v-for="script in scripts" :key="script.name"><span><strong>{{ script.name }}</strong><small>{{ script.description || '设备脚本' }}</small></span><span class="button-group"><button class="table-action" @click="editScript(script)">编辑</button><button class="table-action" @click="exportScript(script)">导出</button><button class="table-action danger-text" @click="removeScript(script)">删除</button></span></div><p v-if="!scripts.length" class="empty-copy">暂无自动化脚本</p></div>
        </section>
      </section>
    </template>
  </div>
</template>

<style scoped>
.feature-page { display: grid; gap: 14px; color: #24303d; }.feature-loading { min-height: 260px; display: grid; place-items: center; align-content: center; gap: 10px; color: #6b7280; font-size: 12px; }.feature-notice { padding: 10px 12px; border: 1px solid var(--accent-border); border-radius: 6px; color: var(--accent-ink); background: var(--accent-soft); font-size: 12px; }.feature-grid { display: grid; gap: 14px; }.two-columns { grid-template-columns: repeat(2, minmax(0, 1fr)); }.metrics-3 { grid-template-columns: repeat(3, minmax(0, 1fr)); }.sms-layout, .at-layout { grid-template-columns: minmax(0, 1.25fr) minmax(300px, .75fr); }.feature-card { min-width: 0; padding: 20px; border: 1px solid #e2e8f0; border-radius: 7px; background: #fff; }.feature-card > span, .feature-card > strong { display: block; }.feature-card > span { color: #6b7280; font-size: 11px; }.feature-card > strong { margin-top: 10px; color: #17212b; font-size: 23px; font-weight: 650; }.feature-card h3 { margin: 0; color: #17212b; font-size: 14px; font-weight: 650; }.feature-card p { margin: 5px 0 0; color: #6b7280; font-size: 11px; }.feature-card-header, .settings-row, .button-group, .command-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }.feature-card-header { margin-bottom: 18px; }.feature-card-header > div p { margin-top: 4px; }.primary-button, .secondary-button, .danger-button, .table-action { border-radius: 6px; font-size: 12px; font-weight: 650; }.primary-button, .secondary-button, .danger-button { min-height: 36px; padding: 0 13px; }.primary-button { border: 1px solid var(--accent); color: #fff; background: var(--accent); }.secondary-button { border: 1px solid #d1d5db; color: #374151; background: #fff; }.danger-button { border: 1px solid #e11d48; color: #fff; background: #e11d48; }.table-action { padding: 4px 0; border: 0; color: var(--accent); background: transparent; }.state-chip { display: inline-flex; align-items: center; min-height: 22px; padding: 0 8px; border: 1px solid var(--accent-border); border-radius: 999px; color: var(--accent-ink); background: var(--accent-soft); font-size: 10px; font-weight: 650; }.state-chip.inactive { border-color: #d1d5db; color: #6b7280; background: #f3f4f6; }.data-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 0 20px; }.data-grid.compact { grid-template-columns: repeat(2, minmax(0, 1fr)); }.data-grid div { padding: 12px 0; border-top: 1px solid #eef0f2; }.data-grid dt { color: #6b7280; font-size: 11px; }.data-grid dd { margin: 5px 0 0; color: #17212b; font-size: 12px; font-weight: 600; overflow-wrap: anywhere; }.segment-control { display: grid; grid-template-columns: repeat(4, 1fr); padding: 3px; border: 1px solid #e5e7eb; border-radius: 6px; background: #f8fafc; }.segment-control button { min-height: 34px; border: 0; border-radius: 4px; color: #6b7280; background: transparent; font-size: 12px; }.segment-control button.selected { color: var(--accent-ink); background: #fff; box-shadow: 0 1px 3px rgba(15,23,42,.12); font-weight: 650; }.switch-list { display: grid; }.switch-list label { display: flex; align-items: center; justify-content: space-between; min-height: 49px; border-bottom: 1px solid #eef0f2; color: #374151; font-size: 12px; }.switch-list input, .inline-switch input { width: 17px; height: 17px; accent-color: var(--accent); }.form-row { margin-top: 16px; }.form-grid { display: grid; gap: 14px; }.form-grid label, .inline-input { display: grid; gap: 6px; color: #4b5563; font-size: 11px; font-weight: 650; }.form-grid input, .form-grid select, .form-grid textarea, .inline-input input, .command-row input { width: 100%; min-height: 36px; padding: 8px 10px; border: 1px solid #d1d5db; border-radius: 5px; outline: none; color: #17212b; background: #fff; font-size: 12px; }.form-grid input:focus, .form-grid select:focus, .form-grid textarea:focus, .inline-input input:focus, .command-row input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }.inline-switch { display: inline-flex; align-items: center; gap: 8px; color: #4b5563; font-size: 12px; }.simple-list, .message-list, .history-list { display: grid; }.simple-list > button, .simple-list > div { width: 100%; display: flex; align-items: center; justify-content: space-between; gap: 12px; min-height: 55px; padding: 8px 0; border: 0; border-bottom: 1px solid #eef0f2; color: #374151; background: transparent; text-align: left; }.simple-list strong, .simple-list small { display: block; }.simple-list strong { color: #374151; font-size: 12px; }.simple-list small { margin-top: 3px; color: #9ca3af; font-size: 10px; }.empty-copy { padding: 18px 0; color: #9ca3af !important; font-size: 12px !important; text-align: center; }.data-table { display: grid; }.table-row { display: grid; grid-template-columns: 1.1fr .75fr 1.2fr 1fr auto; align-items: center; gap: 12px; min-height: 50px; border-bottom: 1px solid #eef0f2; color: #4b5563; font-size: 11px; }.table-row.table-head { min-height: 32px; color: #9ca3af; font-size: 10px; }.table-row strong { color: #17212b; font-size: 12px; }.state-dot { width: 6px; height: 6px; display: inline-block; margin-right: 6px; border-radius: 50%; background: #10b981; }.state-dot.muted { background: #9ca3af; }.band-groups { display: grid; gap: 16px; }.band-groups > div > span { color: #6b7280; font-size: 11px; font-weight: 650; }.band-list { display: flex; flex-wrap: wrap; gap: 7px; margin-top: 7px; }.band-list button { min-width: 48px; min-height: 32px; padding: 0 9px; border: 1px solid #d8dee5; border-radius: 5px; color: #4b5563; background: #fff; font-size: 11px; }.band-list button.selected { border-color: var(--accent); color: var(--accent-ink); background: var(--accent-soft); }.cell-list { display: grid; }.cell-list button { display: grid; grid-template-columns: 1.2fr 1fr 1.2fr 1fr; align-items: center; gap: 12px; min-height: 57px; padding: 10px 2px; border: 0; border-bottom: 1px solid #eef0f2; color: #4b5563; background: transparent; text-align: left; font-size: 11px; }.cell-list button.selected { color: var(--accent-ink); background: var(--accent-soft); }.cell-title strong, .cell-title small { display: block; }.cell-title strong { font-size: 12px; }.cell-title small { margin-top: 3px; color: #9ca3af; }.message-list article { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; gap: 12px; align-items: start; padding: 13px 0; border-bottom: 1px solid #eef0f2; }.message-list strong { color: #374151; font-size: 12px; }.message-list p { max-width: 500px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.message-list time { color: #9ca3af; font-size: 10px; white-space: nowrap; }.message-list button { border: 0; color: #9ca3af; background: transparent; }.settings-row { flex-wrap: wrap; padding: 16px 20px; }.settings-row .button-group { margin-left: auto; }.card-copy { margin: 14px 0 20px !important; }.quick-command { display: flex; flex-wrap: wrap; gap: 7px; margin-bottom: 12px; }.quick-command button { min-height: 29px; padding: 0 8px; border: 1px solid #d8dee5; border-radius: 4px; color: #4b5563; background: #fff; font: 11px ui-monospace, SFMono-Regular, Menlo, monospace; }.command-row { align-items: stretch; }.command-row .primary-button { flex: 0 0 auto; }.command-output { min-height: 170px; margin: 14px 0 0; padding: 14px; overflow: auto; border-radius: 5px; color: #d1fae5; background: #10231e; font: 12px/1.55 ui-monospace, SFMono-Regular, Menlo, monospace; white-space: pre-wrap; }.history-list > div { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 12px 0; border-bottom: 1px solid #eef0f2; }.history-list strong { color: #374151; font: 11px ui-monospace, SFMono-Regular, Menlo, monospace; }.history-list small { color: #9ca3af; font-size: 10px; }.terminal-panel { overflow: hidden; border: 1px solid #263342; border-radius: 7px; background: #101820; }.terminal-bar { height: 42px; display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 0 14px; color: #d7e0ea; background: #1b2835; font-size: 12px; }.terminal-bar span { display: flex; gap: 5px; }.terminal-bar i { width: 8px; height: 8px; border-radius: 50%; background: #f87171; }.terminal-bar i:nth-child(2) { background: #fbbf24; }.terminal-bar i:nth-child(3) { background: #34d399; }.terminal-bar a { color: #a7f3d0; text-decoration: none; font-size: 11px; }.terminal-panel iframe { width: 100%; height: 620px; display: block; border: 0; background: #101820; }.mode-value { color: var(--accent-ink) !important; font-size: 21px !important; }.usb-mode-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; }.usb-mode-grid article { display: grid; gap: 8px; min-height: 160px; padding: 16px; border: 1px solid #e2e8f0; border-radius: 6px; }.usb-mode-grid article.selected { border-color: var(--accent); background: var(--accent-soft); }.usb-mode-grid strong { color: #17212b; font-size: 13px; }.usb-mode-grid span { color: #6b7280; font-size: 11px; }.usb-mode-grid .button-group { align-self: end; flex-wrap: wrap; }
.compact-form { margin-bottom: 18px; }.file-button { position: relative; cursor: pointer; }.file-button input { position: absolute; width: 1px; height: 1px; overflow: hidden; opacity: 0; }.danger-text { color: #e11d48 !important; }.update-upload { display: flex; align-items: center; justify-content: space-between; gap: 18px; }.update-upload p { margin-top: 6px; }
@media (max-width: 900px) { .two-columns, .sms-layout, .at-layout { grid-template-columns: 1fr; }.metrics-3 { grid-template-columns: repeat(3, minmax(0, 1fr)); }.data-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }.usb-mode-grid { grid-template-columns: 1fr; } }
@media (max-width: 640px) { .feature-card { padding: 16px; }.metrics-3 { grid-template-columns: 1fr; }.data-grid, .data-grid.compact { grid-template-columns: 1fr; }.feature-card-header, .settings-row, .update-upload { align-items: flex-start; flex-direction: column; }.settings-row .button-group { margin-left: 0; }.data-table { overflow-x: auto; }.table-row { min-width: 620px; }.cell-list { overflow-x: auto; }.cell-list button { min-width: 520px; }.message-list article { grid-template-columns: minmax(0, 1fr) auto; }.message-list article button { grid-column: 2; }.terminal-panel iframe { height: 480px; } }
</style>
