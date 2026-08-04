<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { authFetch } from '../composables/useApi'
import PluginRunner from './PluginRunner.vue'

const props = defineProps({ page: { type: String, required: true } })

const busy = ref(false)
const notice = ref('')
const system = ref({})
const modemBand = ref({})
const network = ref({ airplane: false, mode: 'nr_5g_lte_auto', slot: 'slot1', data: false, roaming: false, isRoaming: false })
const interfaces = ref([])
const interfaceStats = ref({})
const apn = ref({ mode: 0, template_id: 0, auto_start: 0, template: null })
const apnTemplates = ref([])
const apnTemplateForm = ref({ id: null, name: '', apn: '', protocol: 'dual', username: '', password: '', auth_method: 'chap' })
const bands = ref({ '4G_TDD': [], '4G_FDD': [], '5G': [] })
const selectedBands = ref([])
const cells = ref([])
const selectedCell = ref(null)
const messages = ref([])
const selectedMessage = ref(null)
const sentMessages = ref([])
const smsForm = ref({ recipient: '', content: '' })
const smsConfig = ref({ max_count: 150, max_sent_count: 50 })
const smsFix = ref(false)
const smsLogs = ref({ webhook: '', email: '' })
const emailConfig = ref({ enabled: false, server: '', port: 465, username: '', password: '', from_addr: '', to_addr: '', password_set: false })
const webhookConfig = ref({ enabled: false, url: '', method: 'POST', body: '', headers: '' })
const traffic = ref({ rx: 0, tx: 0, total: 0, limitEnabled: false, limitGb: 10 })
const rathole = ref({ config: { server_addr: '', auto_start: 0, enabled: 0 }, status: {}, services: [] })
const ratholeServiceForm = ref({ id: null, name: '', token: '', local_addr: '', enabled: 1 })
const ratholeExtras = ref({ logs: '', serverConfig: '' })
const ipv6 = ref({ config: { enabled: 0, auto_start: 0, send_enabled: 0, send_interval: 60, webhook_url: '', webhook_body: '', webhook_headers: '' }, status: {}, rules: [] })
const ipv6RuleForm = ref({ id: null, local_port: '', ipv6_port: '', enabled: 1 })
const ipv6Logs = ref('')
const update = ref({ version: '', available: null, latest: '', url: '' })
const updateFile = ref(null)
const rebootSchedule = ref({ enabled: false, time: '03:00', days: ['1', '2', '3', '4', '5', '6', '0'], job: '' })
const rebootDays = [
  { value: '1', label: '一' }, { value: '2', label: '二' }, { value: '3', label: '三' }, { value: '4', label: '四' },
  { value: '5', label: '五' }, { value: '6', label: '六' }, { value: '0', label: '日' }
]
const modemProfileFile = ref(null)
const modemProfile = ref({
  name: '', imei_query: 'AT+CGSN', imei_set: '', iccid_query: 'AT+CCID',
  imsi_query: 'AT+CIMI', qos_query: 'AT+CGEQOSRDP',
  qos_response_prefix: '+CGEQOSRDP:', qos_qci_index: 1,
  qos_downlink_index: 6, qos_uplink_index: 7, airplane_query: 'AT+CFUN?'
})
const systemTime = ref('')
const passwordForm = ref({ old_password: '', new_password: '', confirm: '' })
const securityStatus = ref({ is_set: false })
const securityQuestions = ref({ question1: '', question2: '' })
const securitySetup = ref({ question1: '', answer1: '', question2: '', answer2: '' })
const securityCheck = ref({ answer1: '', answer2: '', confirm: '' })
const at = ref({ command: '', output: '', history: [] })
const usb = ref({ mode_value: null, is_temporary: false })
const plugins = ref([])
const activePlugin = ref(null)
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
    if (props.page === 'modem') await loadModem()
    if (props.page === 'network') await loadNetwork()
    if (props.page === 'netif') interfaces.value = (await api('/netif/list')).interfaces || []
    if (props.page === 'apn') await loadApn()
    if (props.page === 'advanced') await loadRadio()
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

async function loadModem() {
  const [info, band] = await Promise.all([api('/info'), api('/current_band').catch(() => null)])
  system.value = info || {}
  modemBand.value = band ? unwrap(band) || {} : {}
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
  const [inbox, sent, config, email, webhook, fix] = await Promise.all([api('/sms'), api('/sms/sent'), api('/sms/config'), api('/sms/email'), api('/sms/webhook'), api('/sms/fix')])
  messages.value = Array.isArray(inbox) ? inbox : unwrap(inbox) || []
  sentMessages.value = Array.isArray(sent) ? sent : unwrap(sent) || []
  smsConfig.value = { ...smsConfig.value, ...(unwrap(config) || {}) }
  smsFix.value = Boolean(unwrap(fix)?.enabled)
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
  const [profile, time, reboot, security, questions] = await Promise.all([api('/modem-profile'), api('/get/time'), api('/get/first-reboot').catch(() => null), api('/security/status').catch(() => null), api('/security/questions').catch(() => null)])
  modemProfile.value = { ...modemProfile.value, ...(unwrap(profile) || {}) }
  systemTime.value = unwrap(time)?.time || unwrap(time)?.current_time || ''
  applyRebootSchedule(unwrap(reboot) || {})
  securityStatus.value = unwrap(security) || securityStatus.value
  securityQuestions.value = unwrap(questions) || securityQuestions.value
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

function percent(used, total) {
  const numerator = Number(used || 0)
  const denominator = Number(total || 0)
  return denominator > 0 ? Math.max(0, Math.min(100, Math.round(numerator / denominator * 100))) : 0
}

function stringify(value) {
  return typeof value === 'string' ? value : JSON.stringify(value ?? {}, null, 2)
}

async function confirmAction(message) {
  return window.confirm(message)
}

function resetApnTemplateForm() {
  apnTemplateForm.value = { id: null, name: '', apn: '', protocol: 'dual', username: '', password: '', auth_method: 'chap' }
}

function editApnTemplate(template) {
  apnTemplateForm.value = { ...template }
}

async function saveApnTemplate() {
  const form = apnTemplateForm.value
  if (!form.name?.trim() || !form.apn?.trim()) return feedback('请填写模板名称和 APN', true)
  await act(form.id ? 'APN 模板更新' : 'APN 模板创建', async () => {
    if (form.id) await put(`/apn/templates/${form.id}`, form)
    else await post('/apn/templates', form)
    resetApnTemplateForm()
  })
}

async function applyApnTemplate(template) {
  await act('APN 模板应用', async () => {
    await post('/apn/config', { mode: 1, template_id: template.id, auto_start: Number(apn.value.auto_start) || 0 })
    await post('/apn/apply', { template_id: template.id })
  })
}

async function saveApnMode() {
  await act('APN 模式保存', async () => {
    const mode = Number(apn.value.mode) || 0
    const templateId = mode === 1 ? Number(apn.value.template_id) : 0
    if (mode === 1 && !templateId) throw new Error('手动模式请选择一个 APN 模板')
    await post('/apn/config', { mode, template_id: templateId, auto_start: Number(apn.value.auto_start) || 0 })
    if (mode === 0) await post('/apn/clear')
    else await post('/apn/apply', { template_id: templateId })
  })
}

async function removeApnTemplate(template) {
  if (!await confirmAction(`删除 APN 模板“${template.name}”？`)) return
  await act('APN 模板删除', () => api(`/apn/templates/${template.id}`, { method: 'DELETE' }))
}

async function loadSmsLog(type) {
  const path = type === 'webhook' ? '/sms/webhook/logs?lines=50' : '/sms/email/logs?lines=50'
  await act(type === 'webhook' ? 'Webhook 日志刷新' : '邮件日志刷新', async () => {
    smsLogs.value[type] = stringify(unwrap(await api(path)))
  })
}

async function saveSmsConfig() {
  await act('短信存储设置保存', () => post('/sms/config', smsConfig.value))
}

async function toggleSmsFix() {
  const enabled = smsFix.value
  await act(enabled ? '短信接收修复开启' : '短信接收修复关闭', () => post('/sms/fix', { enabled }))
}

async function removeSentMessage(message) {
  if (!await confirmAction('删除这条发送记录？')) return
  await act('发送记录删除', () => api(`/sms/sent/${message.id}`, { method: 'DELETE' }))
}

async function saveRatholeService() {
  const form = ratholeServiceForm.value
  if (!form.name.trim() || !form.token.trim() || !form.local_addr.trim()) return feedback('请填写服务名称、Token 和本地地址', true)
  await act(form.id ? '转发服务更新' : '转发服务创建', async () => {
    if (form.id) await put(`/rathole/services/${form.id}`, form)
    else await post('/rathole/services', form)
    ratholeServiceForm.value = { id: null, name: '', token: '', local_addr: '', enabled: 1 }
  })
}

function editRatholeService(service) {
  ratholeServiceForm.value = { ...service }
}

async function removeRatholeService(service) {
  if (!await confirmAction(`删除转发服务“${service.name}”？`)) return
  await act('转发服务删除', () => api(`/rathole/services/${service.id}`, { method: 'DELETE' }))
}

async function loadRatholeExtra(type) {
  const path = type === 'logs' ? '/rathole/logs?lines=100' : '/rathole/server-config'
  await act(type === 'logs' ? 'Rathole 日志刷新' : '服务端配置生成', async () => {
    const data = unwrap(await api(path))
    if (type === 'logs') ratholeExtras.value.logs = data?.logs || stringify(data)
    else ratholeExtras.value.serverConfig = data?.config || stringify(data)
  })
}

async function saveIpv6Rule() {
  const form = ipv6RuleForm.value
  if (!Number(form.local_port) || !Number(form.ipv6_port)) return feedback('请填写有效的本地端口和 IPv6 端口', true)
  await act(form.id ? 'IPv6 规则更新' : 'IPv6 规则创建', async () => {
    if (form.id) await put(`/ipv6-proxy/rules/${form.id}`, form)
    else await post('/ipv6-proxy/rules', form)
    ipv6RuleForm.value = { id: null, local_port: '', ipv6_port: '', enabled: 1 }
  })
}

function editIpv6Rule(rule) {
  ipv6RuleForm.value = { ...rule }
}

async function removeIpv6Rule(rule) {
  if (!await confirmAction(`删除端口转发规则 ${rule.local_port} -> ${rule.ipv6_port}？`)) return
  await act('IPv6 规则删除', () => api(`/ipv6-proxy/rules/${rule.id}`, { method: 'DELETE' }))
}

async function loadIpv6Logs() {
  await act('IPv6 发送日志刷新', async () => { ipv6Logs.value = stringify(unwrap(await api('/ipv6-proxy/send-logs?lines=50'))) })
}

async function setupSecurity() {
  const form = securitySetup.value
  if (!form.question1.trim() || !form.answer1.trim() || !form.question2.trim() || !form.answer2.trim()) return feedback('请填写两组密保问题和答案', true)
  await act('密保设置', () => post('/security/setup', form))
}

async function verifySecurity() {
  await act('密保验证', () => post('/security/verify', securityCheck.value))
}

async function resetPasswordBySecurity() {
  if (!await confirmAction('将管理密码恢复为默认值，确认继续？')) return
  await act('密码恢复默认值', () => post('/security/reset-password', securityCheck.value))
}

async function factoryReset() {
  if (!await confirmAction('恢复出厂设置会清除全部数据，确认继续？')) return
  await act('恢复出厂设置', () => post('/security/factory-reset', securityCheck.value))
}

function memoryUsed() {
  return Math.max(0, Number(system.value.total_ram || 0) - Number(system.value.free_ram || 0) - Number(system.value.cached_ram || 0))
}

function storageUsed() {
  return Math.max(0, Number(system.value.storage_total || 0) - Number(system.value.storage_free || 0))
}

function formatRate(rate) {
  const units = ['kbps', 'Mbps', 'Gbps']
  let value = Number(rate || 0)
  let unit = 0

  if (!Number.isFinite(value) || value <= 0) return '--'
  while (value >= 1000 && unit < units.length - 1) {
    value /= 1000
    unit += 1
  }

  const digits = value >= 100 ? 0 : value >= 10 ? 1 : 2
  return `${Number(value.toFixed(digits))} ${units[unit]}`
}

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

function exportModemProfile() {
  downloadFile(`modem-profile-${Date.now()}.json`, JSON.stringify(modemProfile.value, null, 2), 'application/json')
  feedback('模组档案已导出')
}

function selectModemProfileFile() {
  modemProfileFile.value?.click()
}

async function importModemProfile(event) {
  const file = event.target.files?.[0]
  event.target.value = ''
  if (!file) return
  try {
    const profile = JSON.parse(await file.text())
    if (!profile || Array.isArray(profile) || typeof profile !== 'object') throw new Error('JSON 必须是一个模组档案对象')
    await act('模组档案导入', () => post('/modem-profile', profile))
  } catch (error) {
    feedback(`模组档案导入失败：${error.message}`, true)
  }
}

function applyFm650Profile() {
  modemProfile.value = {
    ...modemProfile.value,
    name: 'Fibocom FM650 / oFono',
    default_modem_path: '/ril_0', slot1_modem_path: '/ril_0', slot2_modem_path: '/ril_1', default_context_path: '/ril_0/context2',
    advanced_network_enabled: true, advanced_strategy: 'list_csv', lte_band_offset: 100, nr_band_prefix: '50',
    cell_lte_rat: 4, cell_nr_rat: 9, cell_serving_value: 1, cell_arfcn_column: 6, cell_pci_column: 7, cell_band_column: 8, cell_sinr_column: 10, cell_rsrp_column: 12, cell_rsrq_column: 13,
    sms_cnmi_enabled: 'AT+CNMI=2,1,0,0,0', sms_cnmi_disabled: 'AT+CNMI=2,0,0,0,0',
    band_query_lte: 'AT+GTACT?', band_query_nr: 'AT+GTACT?', radio_off: 'AT+CFUN=0', radio_on: 'AT+CFUN=1', pdp_reactivate: 'AT+CGACT=1,1',
    band_reset_lte: 'AT+GTACT=2,3,0', band_reset_nr: 'AT+GTACT=14,6,0', band_set_lte: 'AT+GTACT=2,3,%s', band_set_nr: 'AT+GTACT=14,6,%s',
    cell_lte_serving: 'AT+GTCCINFO?', cell_lte_neighbor: 'AT+GTCCINFO?', cell_nr_serving: 'AT+GTCCINFO?', cell_nr_neighbor: 'AT+GTCCINFO?',
    cell_unlock_lte: 'AT+GTCELLLOCK=0', cell_unlock_nr: 'AT+GTCELLLOCK=0', cell_lock: '', cell_lock_lte: 'AT+GTCELLLOCK=2,0,0,%s,%s', cell_lock_nr: 'AT+GTCELLLOCK=2,1,0,%s,%s',
    imei_query: 'AT+CGSN', imei_set: '', iccid_query: 'AT+CCID', imsi_query: 'AT+CIMI', qos_query: 'AT+CGEQOSRDP', qos_response_prefix: '+CGEQOSRDP:', qos_qci_index: 1, qos_downlink_index: 6, qos_uplink_index: 7, airplane_query: 'AT+CFUN?'
  }
  feedback('FM650 预设已载入，请保存后生效')
}

function applyRebootSchedule(config) {
  const job = String(config?.job || '').trim()
  if (!config?.success || !job) {
    rebootSchedule.value = { ...rebootSchedule.value, enabled: false, job: '' }
    return
  }
  const fields = job.split(/\s+/)
  const minute = Number(fields[0])
  const hour = Number(fields[1])
  const day = fields[4] || '*'
  const days = day === '*' ? rebootDays.map(item => item.value) : day.split(',').filter(item => rebootDays.some(dayItem => dayItem.value === item))
  rebootSchedule.value = {
    enabled: true,
    time: `${String(Number.isFinite(hour) ? hour : 3).padStart(2, '0')}:${String(Number.isFinite(minute) ? minute : 0).padStart(2, '0')}`,
    days: days.length ? days : rebootDays.map(item => item.value),
    job
  }
}

function toggleRebootDay(day) {
  rebootSchedule.value.days = rebootSchedule.value.days.includes(day)
    ? rebootSchedule.value.days.filter(item => item !== day)
    : [...rebootSchedule.value.days, day]
}

function toggleAllRebootDays() {
  rebootSchedule.value.days = rebootSchedule.value.days.length === rebootDays.length ? [] : rebootDays.map(item => item.value)
}

async function saveRebootSchedule() {
  if (!rebootSchedule.value.enabled) {
    await act('定时重启已清除', () => api('/claen/cron'))
    return
  }
  if (!/^\d{2}:\d{2}$/.test(rebootSchedule.value.time) || !rebootSchedule.value.days.length) {
    feedback('请选择重启时间和日期', true)
    return
  }
  const [hour, minute] = rebootSchedule.value.time.split(':')
  await act('定时重启设置已保存', () => api(`/set/reboot?day=${rebootSchedule.value.days.join(',')}&hour=${hour}&minute=${minute}`))
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
      <section class="feature-grid metrics-3"><article class="feature-card"><span>CPU 使用率</span><strong>{{ percent(system.cpu_usage, 100) }}%</strong><p>当前处理器负载</p></article><article class="feature-card"><span>内存使用率</span><strong>{{ percent(memoryUsed(), system.total_ram) }}%</strong><p>{{ memoryUsed() }} MB 已用 / {{ system.total_ram || '--' }} MB</p></article><article class="feature-card"><span>存储使用率</span><strong>{{ percent(storageUsed(), system.storage_total) }}%</strong><p>{{ storageUsed() }} MB 已用 / {{ system.storage_total || '--' }} MB</p></article></section>
      <section class="feature-card"><div class="feature-card-header"><div><h3>设备运行状态</h3><p>系统资源和设备基础信息</p></div><div class="button-group"><button class="secondary-button" @click="act('缓存清理', () => post('/clear_cache'))">清理缓存</button><button class="table-action" @click="load">刷新</button></div></div><dl class="data-grid"><div><dt>主机名</dt><dd>{{ system.hostname || '--' }}</dd></div><div><dt>设备序列号</dt><dd>{{ system.serial || '--' }}</dd></div><div><dt>系统</dt><dd>{{ system.sysname || '--' }}</dd></div><div><dt>内核版本</dt><dd>{{ system.release || '--' }}</dd></div><div><dt>系统版本</dt><dd>{{ system.version || '--' }}</dd></div><div><dt>硬件架构</dt><dd>{{ system.machine || '--' }}</dd></div><div><dt>空闲内存</dt><dd>{{ system.free_ram ?? '--' }} MB</dd></div><div><dt>缓存内存</dt><dd>{{ system.cached_ram ?? '--' }} MB</dd></div><div><dt>可用存储</dt><dd>{{ system.storage_free ?? '--' }} MB</dd></div><div><dt>设备温度</dt><dd>{{ system.thermal_temp ?? system.temperature ?? '--' }} °C</dd></div><div><dt>运行时间</dt><dd>{{ system.uptime ? `${Math.floor(system.uptime / 86400)} 天 ${Math.floor(system.uptime % 86400 / 3600)} 小时` : '--' }}</dd></div></dl></section>
    </template>

    <template v-else-if="page === 'modem'">
      <section class="feature-grid metrics-3"><article class="feature-card"><span>模组型号</span><strong>{{ system.modem_model || '--' }}</strong><p>{{ system.modem_manufacturer || '未识别厂商' }}</p></article><article class="feature-card"><span>当前运营商</span><strong>{{ system.operator || system.carrier || '--' }}</strong><p>{{ system.network_type || '未连接' }}</p></article><article class="feature-card"><span>SIM 卡槽</span><strong>{{ system.sim_slot || '--' }}</strong><p>{{ system.airplane_mode ? '飞行模式已开启' : '蜂窝服务可用' }}</p></article></section>
      <section class="feature-card"><div class="feature-card-header"><div><h3>模组与 SIM 信息</h3><p>模组识别和 SIM 卡身份信息</p></div><button class="table-action" @click="load">刷新</button></div><dl class="data-grid"><div><dt>模组厂商</dt><dd>{{ system.modem_manufacturer || '--' }}</dd></div><div><dt>模组型号</dt><dd>{{ system.modem_model || '--' }}</dd></div><div><dt>修订版本</dt><dd>{{ system.modem_revision || '--' }}</dd></div><div><dt>IMEI</dt><dd>{{ system.imei || '--' }}</dd></div><div><dt>ICCID</dt><dd>{{ system.iccid || '--' }}</dd></div><div><dt>IMSI</dt><dd>{{ system.imsi || '--' }}</dd></div></dl></section>
      <section class="feature-card"><div class="feature-card-header"><div><h3>蜂窝网络状态</h3><p>当前注册网络、服务小区与签约速率</p></div><button class="table-action" @click="load">刷新</button></div><dl class="data-grid"><div><dt>运营商</dt><dd>{{ system.operator || system.carrier || '--' }}</dd></div><div><dt>网络制式</dt><dd>{{ modemBand.network_type || system.network_type || '--' }}</dd></div><div><dt>频段</dt><dd>{{ modemBand.band || system.network_band || '--' }}</dd></div><div><dt>频点 ARFCN</dt><dd>{{ modemBand.arfcn ?? '--' }}</dd></div><div><dt>小区 PCI</dt><dd>{{ modemBand.pci ?? '--' }}</dd></div><div><dt>QCI</dt><dd>{{ system.qci ?? '--' }}</dd></div><div><dt>下行签约速率</dt><dd>{{ formatRate(system.downlink_rate) }}</dd></div><div><dt>上行签约速率</dt><dd>{{ formatRate(system.uplink_rate) }}</dd></div><div><dt>RSRP</dt><dd>{{ modemBand.rsrp ?? system.rsrp ?? '--' }} dBm</dd></div><div><dt>RSRQ</dt><dd>{{ modemBand.rsrq ?? system.rsrq ?? '--' }} dB</dd></div><div><dt>SINR</dt><dd>{{ modemBand.sinr ?? system.sinr ?? '--' }} dB</dd></div></dl></section>
    </template>

    <template v-else-if="page === 'network'">
      <section class="feature-grid two-columns"><article class="feature-card"><div class="feature-card-header"><h3>网络模式</h3><span class="state-chip">{{ system.network_type || '未连接' }}</span></div><div class="segment-control"><button v-for="item in [{ id: 'nr_5g_lte_auto', label: '自动' }, { id: 'nr_5g_only', label: '5G' }, { id: 'lte_only', label: '4G' }, { id: 'nsa_only', label: 'NSA' }]" :key="item.id" :class="{ selected: network.mode === item.id }" @click="act('网络模式切换', () => post('/set_network', { mode: item.id }))">{{ item.label }}</button></div><dl class="data-grid compact"><div><dt>运营商</dt><dd>{{ system.operator || system.carrier || '--' }}</dd></div><div><dt>SIM 卡槽</dt><dd>{{ network.slot }}</dd></div></dl></article><article class="feature-card"><h3>连接开关</h3><div class="switch-list"><label><span>飞行模式</span><input v-model="network.airplane" type="checkbox" @change="act('飞行模式更新', () => post('/airplane_mode', { enabled: network.airplane }))"></label><label><span>蜂窝数据</span><input v-model="network.data" type="checkbox" @change="act('蜂窝数据更新', () => post('/data', { active: network.data }))"></label><label><span>允许数据漫游</span><input v-model="network.roaming" type="checkbox" @change="act('漫游设置更新', () => post('/roaming', { allowed: network.roaming }))"></label></div><div class="form-row"><button class="secondary-button" @click="act('SIM 卡槽切换', () => post('/switch', { slot: network.slot === 'slot1' ? 'slot2' : 'slot1' }))">切换 SIM 卡槽</button></div></article></section>
    </template>

    <template v-else-if="page === 'netif'">
      <section class="feature-card"><div class="feature-card-header"><h3>网络接口</h3><button class="table-action" @click="load">刷新</button></div><div class="data-table"><div class="table-row table-head"><span>接口</span><span>状态</span><span>IPv4</span><span>实时速率</span><span>监听</span></div><div v-for="item in interfaces" :key="item.name" class="table-row"><strong>{{ item.name }}</strong><span><i class="state-dot" :class="{ muted: item.status !== 'up' }"></i>{{ item.status }}</span><span>{{ item.ipv4 || '--' }}</span><span>{{ bytes(interfaceStats[item.name]?.rx_rate || 0) }}/s</span><span class="button-group"><label class="inline-switch"><input :checked="item.monitoring" type="checkbox" @change="act(item.monitoring ? '接口监听已关闭' : '接口监听已开启', async () => { await post('/netif/monitor', { interface: item.name, enabled: !item.monitoring }); item.monitoring = !item.monitoring; if (item.monitoring) interfaceStats[item.name] = await api('/netif/stats', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ interface: item.name }) }) })"><span>{{ item.monitoring ? '已开启' : '已关闭' }}</span></label><button class="table-action" @click="act('接口统计刷新', async () => { interfaceStats[item.name] = await api('/netif/stats', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ interface: item.name }) }) })">详情</button></span></div></div></section>
    </template>

    <template v-else-if="page === 'apn'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="saveApnMode"><div class="feature-card-header"><h3>接入模式</h3><button class="primary-button">保存并应用</button></div><label>模式<select v-model.number="apn.mode"><option :value="0">自动获取</option><option :value="1">使用 APN 模板</option></select></label><label v-if="apn.mode === 1">APN 模板<select v-model.number="apn.template_id"><option :value="0">请选择模板</option><option v-for="item in apnTemplates" :key="item.id" :value="item.id">{{ item.name }} ({{ item.apn }})</option></select></label><label class="inline-switch"><input v-model="apn.auto_start" true-value="1" false-value="0" type="checkbox"><span>开机自动应用</span></label><p class="card-copy">{{ apn.mode === 0 ? '由系统自动获取运营商 APN。' : '保存后会立即应用所选模板。' }}</p></form><section class="feature-card"><div class="feature-card-header"><h3>APN 模板</h3><span>{{ apnTemplates.length }} 个</span></div><div class="simple-list"><div v-for="item in apnTemplates" :key="item.id"><span><strong>{{ item.name }}</strong><small>{{ item.apn }} · {{ item.protocol }}</small></span><span class="button-group"><button class="table-action" @click="applyApnTemplate(item)">应用</button><button class="table-action" @click="editApnTemplate(item)">编辑</button><button class="table-action danger-text" @click="removeApnTemplate(item)">删除</button></span></div><p v-if="!apnTemplates.length" class="empty-copy">暂无保存的 APN 模板</p></div></section></section><form class="feature-card form-grid compact-form" @submit.prevent="saveApnTemplate"><div class="feature-card-header"><h3>{{ apnTemplateForm.id ? '编辑 APN 模板' : '新建 APN 模板' }}</h3><button v-if="apnTemplateForm.id" class="table-action" type="button" @click="resetApnTemplateForm">取消编辑</button></div><label>模板名称<input v-model="apnTemplateForm.name" placeholder="中国移动"></label><label>APN<input v-model="apnTemplateForm.apn" placeholder="cmnet"></label><label>协议<select v-model="apnTemplateForm.protocol"><option value="dual">IPv4 / IPv6</option><option value="ipv4">IPv4</option><option value="ipv6">IPv6</option></select></label><label>认证方式<select v-model="apnTemplateForm.auth_method"><option value="chap">CHAP</option><option value="pap">PAP</option><option value="none">无认证</option></select></label><label>用户名<input v-model="apnTemplateForm.username"></label><label>密码<input v-model="apnTemplateForm.password" type="password"></label><div class="button-group"><button class="primary-button">{{ apnTemplateForm.id ? '保存修改' : '创建模板' }}</button><button type="button" class="secondary-button" @click="act('APN 配置清除', () => post('/apn/clear'))">清除已应用 APN</button></div></form>
    </template>

    <template v-else-if="page === 'advanced'">
      <section class="feature-card"><div class="feature-card-header"><div><h3>频段策略</h3><p>选择频段后应用锁定策略。</p></div><div class="button-group"><button class="secondary-button" @click="act('频段解锁', () => post('/unlock_bands'))">全部解锁</button><button class="primary-button" :disabled="!selectedBands.length" @click="act('频段锁定', () => post('/lock_bands', { bands: selectedBands }))">应用锁定</button></div></div><div class="band-groups"><div v-for="(items, type) in bands" :key="type"><span>{{ type }}</span><div class="band-list"><button v-for="band in items" :key="band" :class="{ selected: selectedBands.includes(band) }" @click="toggleBand(band)">{{ band }}</button></div></div></div></section>
      <section class="feature-card"><div class="feature-card-header"><div><h3>当前与周边小区</h3><p>选择一个目标小区后可直接锁定。</p></div><div class="button-group"><button class="secondary-button" @click="act('小区解锁', () => post('/unlock_cell'))">解除锁定</button><button class="primary-button" :disabled="!selectedCell" @click="act('小区锁定', () => post('/lock_cell', { technology: selectedCell?.rat === 'NR' ? 'NR' : 'LTE', arfcn: String(selectedCell?.arfcn), pci: String(selectedCell?.pci) }))">锁定所选</button></div></div><div class="cell-list"><button v-for="cell in cells" :key="`${cell.rat}-${cell.arfcn}-${cell.pci}`" :class="{ selected: selectedCell?.pci === cell.pci && selectedCell?.arfcn === cell.arfcn }" @click="selectedCell = cell"><span class="cell-title"><strong>{{ cell.isServing ? '服务小区' : '邻区' }}</strong><small>{{ cell.rat }} · {{ cell.band }}</small></span><span>PCI {{ cell.pci }}</span><span>ARFCN {{ cell.arfcn }}</span><span>{{ cell.rsrp }} dBm</span></button></div></section>
    </template>

    <template v-else-if="page === 'sms'">
      <section class="feature-grid sms-layout">
        <section class="feature-card">
          <div class="feature-card-header"><h3>收件箱</h3><button class="table-action" @click="load">刷新</button></div>
          <div class="message-list">
            <article v-for="message in messages" :key="message.id" role="button" tabindex="0" @click="selectedMessage = message" @keyup.enter="selectedMessage = message" @keyup.space.prevent="selectedMessage = message">
              <div><strong>{{ message.sender || message.phone || '未知号码' }}</strong><p>{{ message.content || message.body }}</p></div>
              <time>{{ message.timestamp }}</time>
              <button type="button" title="删除短信" @click.stop="act('短信删除', () => api(`/sms/${message.id}`, { method: 'DELETE' }))"><font-awesome-icon icon="trash" /></button>
            </article>
            <p v-if="!messages.length" class="empty-copy">暂无短信</p>
          </div>
        </section>
        <form class="feature-card form-grid" @submit.prevent="act('短信发送', () => post('/sms/send', smsForm))">
          <div class="feature-card-header"><h3>发送短信</h3><button class="primary-button">发送</button></div>
          <label>收件号码<input v-model="smsForm.recipient" type="tel" placeholder="13800138000"></label>
          <label>短信内容<textarea v-model="smsForm.content" rows="6" maxlength="500"></textarea></label>
        </form>
      </section>
      <section class="feature-grid two-columns">
        <form class="feature-card form-grid" @submit.prevent="act('短信转发设置保存', () => post('/sms/webhook', webhookConfig))">
          <div class="feature-card-header"><h3>Webhook 转发</h3><label class="inline-switch"><input v-model="webhookConfig.enabled" type="checkbox"><span>启用</span></label></div>
          <label>地址<input v-model="webhookConfig.url" type="url" placeholder="https://"></label>
          <label>方法<select v-model="webhookConfig.method"><option>POST</option><option>GET</option></select></label>
          <label>请求内容<textarea v-model="webhookConfig.body" rows="3"></textarea></label>
          <div class="button-group"><button type="button" class="secondary-button" @click="act('Webhook 测试发送', () => post('/sms/webhook/test'))">测试发送</button><button class="primary-button">保存转发设置</button></div>
        </form>
        <form class="feature-card form-grid" @submit.prevent="act('邮件转发设置保存', () => post('/sms/email', emailConfig))">
          <div class="feature-card-header"><h3>邮件转发</h3><label class="inline-switch"><input v-model="emailConfig.enabled" type="checkbox"><span>启用</span></label></div>
          <label>SMTP 主机<input v-model="emailConfig.server" autocomplete="url"></label>
          <label>SMTP 端口<input v-model.number="emailConfig.port" type="number" min="1" max="65535"></label>
          <label>SMTP 用户名<input v-model="emailConfig.username" autocomplete="username"></label>
          <label>SMTP 授权密码<input v-model="emailConfig.password" type="password" autocomplete="new-password" :placeholder="emailConfig.password_set ? '留空则保留已保存的密码' : '邮箱服务商提供的授权密码'"></label>
          <label>发件人邮箱<input v-model="emailConfig.from_addr" type="email" placeholder="router@example.com"></label>
          <label>收件人邮箱<input v-model="emailConfig.to_addr" type="email" placeholder="admin@example.com"></label>
          <div class="button-group"><button type="button" class="secondary-button" @click="act('测试邮件发送', () => post('/sms/email/test'))">发送测试邮件</button><button class="primary-button">保存邮件设置</button></div>
        </form>
      </section>
      <section class="feature-grid two-columns">
        <section class="feature-card"><div class="feature-card-header"><h3>发送记录</h3><span>{{ sentMessages.length }} 条</span></div><div class="simple-list"><div v-for="message in sentMessages" :key="message.id"><span><strong>{{ message.recipient }}</strong><small>{{ message.content }} · {{ message.status || '已发送' }}</small></span><button class="table-action danger-text" @click="removeSentMessage(message)">删除</button></div><p v-if="!sentMessages.length" class="empty-copy">暂无发送记录</p></div></section>
        <form class="feature-card form-grid" @submit.prevent="saveSmsConfig"><div class="feature-card-header"><h3>短信接收设置</h3><label class="inline-switch"><input v-model="smsFix" type="checkbox" @change="toggleSmsFix"><span>接收修复</span></label></div><label>收件箱最大数量<input v-model.number="smsConfig.max_count" type="number" min="10" max="150"></label><label>发件箱最大数量<input v-model.number="smsConfig.max_sent_count" type="number" min="1" max="50"></label><button class="primary-button">保存存储设置</button></form>
      </section>
      <section class="feature-grid two-columns"><details class="feature-card"><summary>Webhook 投递日志</summary><div class="details-content"><button class="secondary-button" @click="loadSmsLog('webhook')">刷新日志</button><pre v-if="smsLogs.webhook" class="command-output">{{ smsLogs.webhook }}</pre></div></details><details class="feature-card"><summary>邮件投递日志</summary><div class="details-content"><button class="secondary-button" @click="loadSmsLog('email')">刷新日志</button><pre v-if="smsLogs.email" class="command-output">{{ smsLogs.email }}</pre></div></details></section>
    </template>

    <template v-else-if="page === 'traffic'">
      <section class="feature-grid metrics-3"><article class="feature-card"><span>下载用量</span><strong>{{ bytes(traffic.rx) }}</strong></article><article class="feature-card"><span>上传用量</span><strong>{{ bytes(traffic.tx) }}</strong></article><article class="feature-card"><span>累计流量</span><strong>{{ bytes(traffic.total) }}</strong></article></section><section class="feature-card"><form class="settings-row" @submit.prevent="act('流量限额保存', () => post('/set/total', { switch: traffic.limitEnabled ? 1 : 0, much: Math.round(traffic.limitGb * 1073741824) }))"><label class="inline-switch"><input v-model="traffic.limitEnabled" type="checkbox"><span>启用流量限额</span></label><label class="inline-input">限额 (GB)<input v-model.number="traffic.limitGb" :disabled="!traffic.limitEnabled" type="number" min="1"></label><div class="button-group"><button type="button" class="secondary-button" @click="act('流量统计清除', () => post('/set/total'))">清除统计</button><button class="primary-button">保存</button></div></form></section>
    </template>

    <template v-else-if="page === 'rathole'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('Rathole 配置保存', () => post('/rathole/config', rathole.config))"><div class="feature-card-header"><h3>客户端配置</h3><span class="state-chip" :class="{ inactive: !rathole.status.running }">{{ rathole.status.running ? '运行中' : '未运行' }}</span></div><label>服务器地址<input v-model="rathole.config.server_addr" placeholder="host:2333"></label><label class="inline-switch"><input v-model="rathole.config.enabled" true-value="1" false-value="0" type="checkbox"><span>启用服务</span></label><label class="inline-switch"><input v-model="rathole.config.auto_start" true-value="1" false-value="0" type="checkbox" @change="act('Rathole 开机启动设置', () => post('/rathole/autostart', { auto_start: Number(rathole.config.auto_start) }))"><span>开机启动</span></label><div class="button-group"><button type="button" class="secondary-button" @click="act('服务启动', () => post('/rathole/start'))">启动</button><button type="button" class="secondary-button" @click="act('服务停止', () => post('/rathole/stop'))">停止</button><button class="primary-button">保存</button></div></form><section class="feature-card"><div class="feature-card-header"><h3>转发服务</h3><span>{{ rathole.services.length }} 个</span></div><div class="simple-list"><div v-for="service in rathole.services" :key="service.id"><span><strong>{{ service.name }}</strong><small>{{ service.local_addr }}</small></span><span class="button-group"><span class="state-chip" :class="{ inactive: !service.enabled }">{{ service.enabled ? '启用' : '停用' }}</span><button class="table-action" @click="editRatholeService(service)">编辑</button><button class="table-action danger-text" @click="removeRatholeService(service)">删除</button></span></div><p v-if="!rathole.services.length" class="empty-copy">暂无转发服务</p></div></section></section><form class="feature-card form-grid compact-form" @submit.prevent="saveRatholeService"><div class="feature-card-header"><h3>{{ ratholeServiceForm.id ? '编辑转发服务' : '新增转发服务' }}</h3><button v-if="ratholeServiceForm.id" type="button" class="table-action" @click="ratholeServiceForm = { id: null, name: '', token: '', local_addr: '', enabled: 1 }">取消编辑</button></div><label>服务名称<input v-model="ratholeServiceForm.name" placeholder="ssh"></label><label>服务 Token<input v-model="ratholeServiceForm.token" type="password"></label><label>本地地址<input v-model="ratholeServiceForm.local_addr" placeholder="127.0.0.1:22"></label><label class="inline-switch"><input v-model="ratholeServiceForm.enabled" true-value="1" false-value="0" type="checkbox"><span>启用此服务</span></label><button class="primary-button">{{ ratholeServiceForm.id ? '保存修改' : '新增服务' }}</button></form><section class="feature-grid two-columns"><details class="feature-card"><summary>运行日志</summary><div class="details-content"><button class="secondary-button" @click="loadRatholeExtra('logs')">刷新日志</button><pre v-if="ratholeExtras.logs" class="command-output">{{ ratholeExtras.logs }}</pre></div></details><details class="feature-card"><summary>服务端配置</summary><div class="details-content"><button class="secondary-button" @click="loadRatholeExtra('server')">生成配置</button><pre v-if="ratholeExtras.serverConfig" class="command-output">{{ ratholeExtras.serverConfig }}</pre></div></details></section>
    </template>

    <template v-else-if="page === 'ipv6'">
      <section class="feature-grid two-columns"><form class="feature-card form-grid" @submit.prevent="act('IPv6 代理配置保存', () => post('/ipv6-proxy/config', ipv6.config))"><div class="feature-card-header"><h3>服务配置</h3><span class="state-chip" :class="{ inactive: !ipv6.status.running }">{{ ipv6.status.running ? '运行中' : '未运行' }}</span></div><label class="inline-switch"><input v-model="ipv6.config.enabled" true-value="1" false-value="0" type="checkbox"><span>启用 IPv6 代理</span></label><label class="inline-switch"><input v-model="ipv6.config.auto_start" true-value="1" false-value="0" type="checkbox"><span>开机启动</span></label><label class="inline-switch"><input v-model="ipv6.config.send_enabled" true-value="1" false-value="0" type="checkbox"><span>定时发送 IPv6 地址</span></label><label>发送周期（秒）<input v-model.number="ipv6.config.send_interval" :disabled="!ipv6.config.send_enabled" type="number" min="10"></label><label>Webhook 地址<input v-model="ipv6.config.webhook_url" type="url" placeholder="https://"></label><label>请求头<textarea v-model="ipv6.config.webhook_headers" rows="2" placeholder="Content-Type: application/json"></textarea></label><label>请求内容<textarea v-model="ipv6.config.webhook_body" rows="3" placeholder="支持 #{ipv6}、#{link}、#{time}"></textarea></label><div class="button-group"><button type="button" class="secondary-button" @click="act('代理启动', () => post('/ipv6-proxy/start'))">启动</button><button type="button" class="secondary-button" @click="act('代理停止', () => post('/ipv6-proxy/stop'))">停止</button><button type="button" class="secondary-button" @click="act('代理重启', () => post('/ipv6-proxy/restart'))">重启</button><button class="primary-button">保存</button></div></form><section class="feature-card"><div class="feature-card-header"><h3>代理状态</h3><span class="state-chip" :class="{ inactive: !ipv6.status.running }">{{ ipv6.status.running ? '运行中' : '未运行' }}</span></div><dl class="data-grid compact"><div><dt>IPv6 地址</dt><dd>{{ ipv6.status.ipv6_addr || '--' }}</dd></div><div><dt>规则数量</dt><dd>{{ ipv6.status.rule_count ?? ipv6.rules.length }}</dd></div><div><dt>活跃连接</dt><dd>{{ ipv6.status.active_count ?? '--' }}</dd></div><div><dt>访问链接</dt><dd>{{ ipv6.status.link || '--' }}</dd></div></dl><div class="button-group"><button class="secondary-button" @click="act('IPv6 地址发送', () => post('/ipv6-proxy/send'))">立即发送</button><button class="secondary-button" @click="act('IPv6 Webhook 测试', () => post('/ipv6-proxy/test'))">测试发送</button><button class="table-action" @click="loadIpv6Logs">查看日志</button></div><pre v-if="ipv6Logs" class="command-output">{{ ipv6Logs }}</pre></section></section><section class="feature-grid two-columns"><section class="feature-card"><div class="feature-card-header"><h3>端口转发规则</h3><span>{{ ipv6.rules.length }} 条</span></div><div class="simple-list"><div v-for="rule in ipv6.rules" :key="rule.id"><span><strong>{{ rule.local_port }} → {{ rule.ipv6_port }}</strong><small>{{ rule.enabled ? '启用' : '停用' }}</small></span><span class="button-group"><button class="table-action" @click="editIpv6Rule(rule)">编辑</button><button class="table-action danger-text" @click="removeIpv6Rule(rule)">删除</button></span></div><p v-if="!ipv6.rules.length" class="empty-copy">暂无代理规则</p></div></section><form class="feature-card form-grid" @submit.prevent="saveIpv6Rule"><div class="feature-card-header"><h3>{{ ipv6RuleForm.id ? '编辑端口规则' : '新增端口规则' }}</h3><button v-if="ipv6RuleForm.id" type="button" class="table-action" @click="ipv6RuleForm = { id: null, local_port: '', ipv6_port: '', enabled: 1 }">取消编辑</button></div><label>本地端口<input v-model.number="ipv6RuleForm.local_port" type="number" min="1" max="65535"></label><label>IPv6 端口<input v-model.number="ipv6RuleForm.ipv6_port" type="number" min="1" max="65535"></label><label class="inline-switch"><input v-model="ipv6RuleForm.enabled" true-value="1" false-value="0" type="checkbox"><span>启用规则</span></label><button class="primary-button">{{ ipv6RuleForm.id ? '保存修改' : '新增规则' }}</button></form></section>
    </template>

    <template v-else-if="page === 'update'">
      <section class="feature-grid two-columns"><article class="feature-card"><span>当前版本</span><strong>{{ update.version || '--' }}</strong><p class="card-copy">{{ update.available ? `发现新版本 ${update.latest}` : '当前已是最新版本' }}</p></article><section class="feature-card"><h3>在线更新</h3><p class="card-copy">从更新源下载、校验并安装系统包。</p><div class="button-group"><button class="secondary-button" @click="act('更新检查', () => api('/update/check'))">检查更新</button><button class="primary-button" :disabled="!update.available" @click="installUpdate">在线安装</button></div></section></section><section class="feature-card update-upload"><div><h3>本地安装包</h3><p>{{ updateFile ? updateFile.name : '尚未选择安装包' }}</p></div><div class="button-group"><label class="secondary-button file-button">选择安装包<input type="file" accept=".tar,.gz,.tgz,.zip,.bin" @change="selectUpdateFile"></label><button class="primary-button" :disabled="!updateFile" @click="installUpdate">上传并安装</button></div></section>
    </template>

    <template v-else-if="page === 'settings'">
      <form class="feature-card form-grid modem-profile-editor" @submit.prevent="act('模组配置保存', () => post('/modem-profile', modemProfile))">
        <div class="feature-card-header">
          <div><h3>模组适配档案</h3><p>保存后立即应用到 AT 指令与 oFono 路径。</p></div>
          <div class="button-group profile-actions">
            <button type="button" class="secondary-button" @click="applyFm650Profile">FM650 预设</button>
            <button type="button" class="secondary-button" @click="exportModemProfile"><font-awesome-icon icon="cloud-download-alt" /> 导出 JSON</button>
            <label class="secondary-button file-button">导入 JSON<input ref="modemProfileFile" type="file" accept="application/json,.json" @change="importModemProfile"></label>
            <button class="primary-button">保存档案</button>
          </div>
        </div>

        <details class="profile-section" open>
          <summary>基础适配</summary>
          <div class="profile-section-body profile-fields">
            <label>配置名称<input v-model="modemProfile.name"></label>
            <label>IMEI 查询指令<input v-model="modemProfile.imei_query"></label>
            <label>IMEI 写入模板<input v-model="modemProfile.imei_set" placeholder="留空则禁用"></label>
            <label>ICCID 查询指令<input v-model="modemProfile.iccid_query" placeholder="留空则不采集"></label>
            <label>IMSI 查询指令<input v-model="modemProfile.imsi_query" placeholder="留空则不采集"></label>
            <label>飞行模式查询指令<input v-model="modemProfile.airplane_query" placeholder="留空则不采集"></label>
            <label>QoS 查询指令<input v-model="modemProfile.qos_query" placeholder="留空则不采集 QCI 和速率"></label>
            <label>QoS 响应前缀<input v-model="modemProfile.qos_response_prefix" placeholder="例如 +CGEQOSRDP:"></label>
            <label>QCI 字段序号<input v-model.number="modemProfile.qos_qci_index" type="number" min="0" max="31"></label>
            <label>下行字段序号<input v-model.number="modemProfile.qos_downlink_index" type="number" min="0" max="31"></label>
            <label>上行字段序号<input v-model.number="modemProfile.qos_uplink_index" type="number" min="0" max="31"></label>
          </div>
        </details>

        <details class="profile-section">
          <summary>oFono 与短信</summary>
          <div class="profile-section-body profile-fields">
            <label>默认 Modem 路径<input v-model="modemProfile.default_modem_path" placeholder="/ril_0"></label>
            <label>SIM 1 Modem 路径<input v-model="modemProfile.slot1_modem_path" placeholder="/ril_0"></label>
            <label>SIM 2 Modem 路径<input v-model="modemProfile.slot2_modem_path" placeholder="/ril_1"></label>
            <label>默认 Context 路径<input v-model="modemProfile.default_context_path" placeholder="/ril_0/context2"></label>
            <label>短信上报启用命令<input v-model="modemProfile.sms_cnmi_enabled"></label>
            <label>短信上报停用命令<input v-model="modemProfile.sms_cnmi_disabled"></label>
          </div>
        </details>

        <details class="profile-section">
          <summary>高级网络策略</summary>
          <div class="profile-section-body profile-fields">
            <label class="inline-switch"><input v-model="modemProfile.advanced_network_enabled" type="checkbox"><span>启用频段和小区操作</span></label>
            <label>指令策略<select v-model="modemProfile.advanced_strategy"><option value="bitmask_matrix">位掩码矩阵</option><option value="list_csv">频段列表 / CSV</option></select></label>
            <label>LTE 频段偏移<input v-model.number="modemProfile.lte_band_offset" type="number"></label>
            <label>NR 频段前缀<input v-model="modemProfile.nr_band_prefix"></label>
            <label>查询 LTE 频段<input v-model="modemProfile.band_query_lte"></label>
            <label>查询 NR 频段<input v-model="modemProfile.band_query_nr"></label>
            <label>关闭无线功能<input v-model="modemProfile.radio_off"></label>
            <label>开启无线功能<input v-model="modemProfile.radio_on"></label>
            <label>重新激活 PDP<input v-model="modemProfile.pdp_reactivate"></label>
            <label>重置 LTE 频段<input v-model="modemProfile.band_reset_lte"></label>
            <label>重置 NR 频段<input v-model="modemProfile.band_reset_nr"></label>
            <label>设置 LTE 频段模板<input v-model="modemProfile.band_set_lte"></label>
            <label>设置 NR 频段模板<input v-model="modemProfile.band_set_nr"></label>
          </div>
        </details>

        <details class="profile-section">
          <summary>小区扫描与锁定</summary>
          <div class="profile-section-body profile-fields">
            <label>LTE RAT 值<input v-model.number="modemProfile.cell_lte_rat" type="number"></label>
            <label>NR RAT 值<input v-model.number="modemProfile.cell_nr_rat" type="number"></label>
            <label>服务小区标记值<input v-model.number="modemProfile.cell_serving_value" type="number"></label>
            <label>ARFCN 列序号<input v-model.number="modemProfile.cell_arfcn_column" type="number" min="0"></label>
            <label>PCI 列序号<input v-model.number="modemProfile.cell_pci_column" type="number" min="0"></label>
            <label>频段列序号<input v-model.number="modemProfile.cell_band_column" type="number" min="0"></label>
            <label>SINR 列序号<input v-model.number="modemProfile.cell_sinr_column" type="number" min="0"></label>
            <label>RSRP 列序号<input v-model.number="modemProfile.cell_rsrp_column" type="number" min="0"></label>
            <label>RSRQ 列序号<input v-model.number="modemProfile.cell_rsrq_column" type="number" min="0"></label>
            <label>LTE 服务小区查询<input v-model="modemProfile.cell_lte_serving"></label>
            <label>LTE 邻区查询<input v-model="modemProfile.cell_lte_neighbor"></label>
            <label>NR 服务小区查询<input v-model="modemProfile.cell_nr_serving"></label>
            <label>NR 邻区查询<input v-model="modemProfile.cell_nr_neighbor"></label>
            <label>LTE 小区解锁<input v-model="modemProfile.cell_unlock_lte"></label>
            <label>NR 小区解锁<input v-model="modemProfile.cell_unlock_nr"></label>
            <label v-if="modemProfile.advanced_strategy === 'bitmask_matrix'">小区锁定模板<input v-model="modemProfile.cell_lock"></label>
            <label v-else>LTE 小区锁定模板<input v-model="modemProfile.cell_lock_lte"></label>
            <label v-if="modemProfile.advanced_strategy === 'list_csv'">NR 小区锁定模板<input v-model="modemProfile.cell_lock_nr"></label>
          </div>
        </details>

        <div class="button-group profile-footer"><button type="button" class="danger-button" @click="act('模组配置重置', () => post('/modem-profile/reset'))">恢复展锐默认档案</button><button class="primary-button">保存档案</button></div>
      </form>

      <section class="feature-grid two-columns">
        <section class="feature-card"><h3>设备控制</h3><dl class="data-grid compact"><div><dt>系统时间</dt><dd>{{ systemTime || '--' }}</dd></div></dl><div class="button-group"><button class="secondary-button" @click="act('时间同步', () => post('/set/time'))">同步时间</button><button class="danger-button" @click="act('设备重启', () => post('/device_control', { action: 'reboot' }))">重启设备</button></div></section>
        <form class="feature-card form-grid reboot-schedule" @submit.prevent="saveRebootSchedule"><div class="feature-card-header"><h3>定时重启</h3><label class="inline-switch"><input v-model="rebootSchedule.enabled" type="checkbox"><span>启用</span></label></div><label>重启时间<input v-model="rebootSchedule.time" :disabled="!rebootSchedule.enabled" type="time"></label><div class="reboot-days" :class="{ disabled: !rebootSchedule.enabled }"><span>重复日期</span><div><button type="button" :class="{ selected: rebootSchedule.days.length === rebootDays.length }" :disabled="!rebootSchedule.enabled" @click="toggleAllRebootDays">每天</button><button v-for="day in rebootDays" :key="day.value" type="button" :class="{ selected: rebootSchedule.days.includes(day.value) }" :disabled="!rebootSchedule.enabled" @click="toggleRebootDay(day.value)">{{ day.label }}</button></div></div><div class="button-group"><button type="button" class="secondary-button" :disabled="!rebootSchedule.job" @click="act('定时重启已清除', () => api('/claen/cron'))">清除计划</button><button class="primary-button">保存计划</button></div></form>
      </section>
      <form class="feature-card settings-row" @submit.prevent="act('密码修改', () => post('/auth/password', passwordForm))"><label class="inline-input">当前密码<input v-model="passwordForm.old_password" type="password" autocomplete="current-password"></label><label class="inline-input">新密码<input v-model="passwordForm.new_password" type="password" autocomplete="new-password"></label><label class="inline-input">确认新密码<input v-model="passwordForm.confirm" type="password" autocomplete="new-password"></label><button class="primary-button">修改密码</button></form>
      <section class="feature-grid two-columns"><form v-if="!securityStatus.is_set" class="feature-card form-grid" @submit.prevent="setupSecurity"><div class="feature-card-header"><h3>密保设置</h3><span class="state-chip inactive">尚未设置</span></div><label>问题一<input v-model="securitySetup.question1" placeholder="例如：出生城市"></label><label>答案一<input v-model="securitySetup.answer1" type="password"></label><label>问题二<input v-model="securitySetup.question2" placeholder="例如：第一所学校"></label><label>答案二<input v-model="securitySetup.answer2" type="password"></label><button class="primary-button">保存密保</button></form><section v-else class="feature-card"><div class="feature-card-header"><h3>密保设置</h3><span class="state-chip">已设置</span></div><dl class="data-grid compact"><div><dt>问题一</dt><dd>{{ securityQuestions.question1 || '--' }}</dd></div><div><dt>问题二</dt><dd>{{ securityQuestions.question2 || '--' }}</dd></div></dl></section><form class="feature-card form-grid" @submit.prevent="verifySecurity"><div class="feature-card-header"><h3>高风险操作验证</h3><span class="state-chip inactive">谨慎操作</span></div><label>答案一<input v-model="securityCheck.answer1" type="password"></label><label>答案二<input v-model="securityCheck.answer2" type="password"></label><label>确认文本<input v-model="securityCheck.confirm" placeholder="已知晓风险"></label><div class="button-group"><button class="secondary-button">验证答案</button><button type="button" class="secondary-button" @click="resetPasswordBySecurity">密码恢复默认值</button><button type="button" class="danger-button" @click="factoryReset">恢复出厂设置</button></div></form></section>
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
          <div class="simple-list"><div v-for="plugin in plugins" :key="plugin.filename || plugin.name"><span><strong>{{ plugin.filename || plugin.name }}</strong><small>{{ plugin.description || '本地扩展插件' }}</small></span><span class="button-group"><button class="table-action" @click="activePlugin = plugin">打开</button><button class="table-action" @click="exportPlugin(plugin)">导出</button><button class="table-action danger-text" @click="removePlugin(plugin)">删除</button></span></div><p v-if="!plugins.length" class="empty-copy">暂无已安装插件</p></div>
        </section>
        <section class="feature-card">
          <div class="feature-card-header"><div><h3>脚本管理</h3><p>{{ scripts.length }} 个自动化脚本</p></div><div class="button-group"><button class="table-action" @click="exportScripts">导出全部</button><label class="table-action file-button">导入<input type="file" accept=".sh,.json,text/x-shellscript,application/json" @change="importScripts"></label></div></div>
          <form class="form-grid compact-form" @submit.prevent="saveScript"><label>脚本名称<input v-model="scriptForm.name" placeholder="startup.sh"></label><label>脚本内容<textarea v-model="scriptForm.content" rows="5" placeholder="#!/bin/sh"></textarea></label><div class="button-group"><button class="primary-button">{{ editingScript ? '保存修改' : '新建脚本' }}</button><button v-if="editingScript" type="button" class="secondary-button" @click="editingScript = null; scriptForm = { name: '', content: '' }">取消编辑</button></div></form>
          <div class="simple-list"><div v-for="script in scripts" :key="script.name"><span><strong>{{ script.name }}</strong><small>{{ script.description || '设备脚本' }}</small></span><span class="button-group"><button class="table-action" @click="editScript(script)">编辑</button><button class="table-action" @click="exportScript(script)">导出</button><button class="table-action danger-text" @click="removeScript(script)">删除</button></span></div><p v-if="!scripts.length" class="empty-copy">暂无自动化脚本</p></div>
        </section>
      </section>
    </template>

    <div v-if="selectedMessage" class="message-modal-backdrop" role="presentation" @click.self="selectedMessage = null">
      <section class="message-modal" role="dialog" aria-modal="true" aria-labelledby="message-detail-title">
        <div class="feature-card-header">
          <div><h3 id="message-detail-title">短信详情</h3><p>{{ selectedMessage.timestamp || '--' }}</p></div>
          <button type="button" class="modal-close" title="关闭短信详情" aria-label="关闭短信详情" @click="selectedMessage = null"><font-awesome-icon icon="times-circle" /></button>
        </div>
        <dl class="message-details">
          <div><dt>发件号码</dt><dd>{{ selectedMessage.sender || selectedMessage.phone || '未知号码' }}</dd></div>
          <div><dt>接收时间</dt><dd>{{ selectedMessage.timestamp || '--' }}</dd></div>
          <div><dt>短信内容</dt><dd class="message-body">{{ selectedMessage.content || selectedMessage.body || '--' }}</dd></div>
        </dl>
      </section>
    </div>
    <PluginRunner v-if="activePlugin" :plugin="activePlugin" @close="activePlugin = null" />
  </div>
</template>

<style scoped>
.feature-page { display: grid; gap: 14px; color: #24303d; }.feature-loading { min-height: 260px; display: grid; place-items: center; align-content: center; gap: 10px; color: #6b7280; font-size: 12px; }.feature-notice { padding: 10px 12px; border: 1px solid var(--accent-border); border-radius: 6px; color: var(--accent-ink); background: var(--accent-soft); font-size: 12px; }.feature-grid { display: grid; gap: 14px; }.two-columns { grid-template-columns: repeat(2, minmax(0, 1fr)); }.metrics-3 { grid-template-columns: repeat(3, minmax(0, 1fr)); }.sms-layout, .at-layout { grid-template-columns: minmax(0, 1.25fr) minmax(300px, .75fr); }.feature-card { min-width: 0; padding: 20px; border: 1px solid #e2e8f0; border-radius: 7px; background: #fff; }.feature-card > span, .feature-card > strong { display: block; }.feature-card > span { color: #6b7280; font-size: 11px; }.feature-card > strong { margin-top: 10px; color: #17212b; font-size: 23px; font-weight: 650; }.feature-card h3 { margin: 0; color: #17212b; font-size: 14px; font-weight: 650; }.feature-card p { margin: 5px 0 0; color: #6b7280; font-size: 11px; }.feature-card-header, .settings-row, .button-group, .command-row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }.feature-card-header { margin-bottom: 18px; }.feature-card-header > div p { margin-top: 4px; }.primary-button, .secondary-button, .danger-button, .table-action { border-radius: 6px; font-size: 12px; font-weight: 650; }.primary-button, .secondary-button, .danger-button { min-height: 36px; padding: 0 13px; }.primary-button { border: 1px solid var(--accent); color: #fff; background: var(--accent); }.secondary-button { border: 1px solid #d1d5db; color: #374151; background: #fff; }.danger-button { border: 1px solid #e11d48; color: #fff; background: #e11d48; }.table-action { padding: 4px 0; border: 0; color: var(--accent); background: transparent; }.state-chip { display: inline-flex; align-items: center; min-height: 22px; padding: 0 8px; border: 1px solid var(--accent-border); border-radius: 999px; color: var(--accent-ink); background: var(--accent-soft); font-size: 10px; font-weight: 650; }.state-chip.inactive { border-color: #d1d5db; color: #6b7280; background: #f3f4f6; }.data-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 0 20px; }.data-grid.compact { grid-template-columns: repeat(2, minmax(0, 1fr)); }.data-grid div { padding: 12px 0; border-top: 1px solid #eef0f2; }.data-grid dt { color: #6b7280; font-size: 11px; }.data-grid dd { margin: 5px 0 0; color: #17212b; font-size: 12px; font-weight: 600; overflow-wrap: anywhere; }.segment-control { display: grid; grid-template-columns: repeat(4, 1fr); padding: 3px; border: 1px solid #e5e7eb; border-radius: 6px; background: #f8fafc; }.segment-control button { min-height: 34px; border: 0; border-radius: 4px; color: #6b7280; background: transparent; font-size: 12px; }.segment-control button.selected { color: var(--accent-ink); background: #fff; box-shadow: 0 1px 3px rgba(15,23,42,.12); font-weight: 650; }.switch-list { display: grid; }.switch-list label { display: flex; align-items: center; justify-content: space-between; min-height: 49px; border-bottom: 1px solid #eef0f2; color: #374151; font-size: 12px; }.switch-list input, .inline-switch input { width: 17px; height: 17px; accent-color: var(--accent); }.form-row { margin-top: 16px; }.form-grid { display: grid; gap: 14px; }.form-grid label, .inline-input { display: grid; gap: 6px; color: #4b5563; font-size: 11px; font-weight: 650; }.form-grid input, .form-grid select, .form-grid textarea, .inline-input input, .command-row input { width: 100%; min-height: 36px; padding: 8px 10px; border: 1px solid #d1d5db; border-radius: 5px; outline: none; color: #17212b; background: #fff; font-size: 12px; }.form-grid input:focus, .form-grid select:focus, .form-grid textarea:focus, .inline-input input:focus, .command-row input:focus { border-color: var(--accent); box-shadow: 0 0 0 3px var(--accent-soft); }.inline-switch { display: inline-flex; align-items: center; gap: 8px; color: #4b5563; font-size: 12px; }.simple-list, .message-list, .history-list { display: grid; }.simple-list > button, .simple-list > div { width: 100%; display: flex; align-items: center; justify-content: space-between; gap: 12px; min-height: 55px; padding: 8px 0; border: 0; border-bottom: 1px solid #eef0f2; color: #374151; background: transparent; text-align: left; }.simple-list strong, .simple-list small { display: block; }.simple-list strong { color: #374151; font-size: 12px; }.simple-list small { margin-top: 3px; color: #9ca3af; font-size: 10px; }.empty-copy { padding: 18px 0; color: #9ca3af !important; font-size: 12px !important; text-align: center; }.data-table { display: grid; }.table-row { display: grid; grid-template-columns: 1.1fr .75fr 1.2fr 1fr auto; align-items: center; gap: 12px; min-height: 50px; border-bottom: 1px solid #eef0f2; color: #4b5563; font-size: 11px; }.table-row.table-head { min-height: 32px; color: #9ca3af; font-size: 10px; }.table-row strong { color: #17212b; font-size: 12px; }.state-dot { width: 6px; height: 6px; display: inline-block; margin-right: 6px; border-radius: 50%; background: #10b981; }.state-dot.muted { background: #9ca3af; }.band-groups { display: grid; gap: 16px; }.band-groups > div > span { color: #6b7280; font-size: 11px; font-weight: 650; }.band-list { display: flex; flex-wrap: wrap; gap: 7px; margin-top: 7px; }.band-list button { min-width: 48px; min-height: 32px; padding: 0 9px; border: 1px solid #d8dee5; border-radius: 5px; color: #4b5563; background: #fff; font-size: 11px; }.band-list button.selected { border-color: var(--accent); color: var(--accent-ink); background: var(--accent-soft); }.cell-list { display: grid; }.cell-list button { display: grid; grid-template-columns: 1.2fr 1fr 1.2fr 1fr; align-items: center; gap: 12px; min-height: 57px; padding: 10px 2px; border: 0; border-bottom: 1px solid #eef0f2; color: #4b5563; background: transparent; text-align: left; font-size: 11px; }.cell-list button.selected { color: var(--accent-ink); background: var(--accent-soft); }.cell-title strong, .cell-title small { display: block; }.cell-title strong { font-size: 12px; }.cell-title small { margin-top: 3px; color: #9ca3af; }.message-list article { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; gap: 12px; align-items: start; padding: 13px 0; border-bottom: 1px solid #eef0f2; }.message-list strong { color: #374151; font-size: 12px; }.message-list p { max-width: 500px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }.message-list time { color: #9ca3af; font-size: 10px; white-space: nowrap; }.message-list button { border: 0; color: #9ca3af; background: transparent; }.settings-row { flex-wrap: wrap; padding: 16px 20px; }.settings-row .button-group { margin-left: auto; }.card-copy { margin: 14px 0 20px !important; }.quick-command { display: flex; flex-wrap: wrap; gap: 7px; margin-bottom: 12px; }.quick-command button { min-height: 29px; padding: 0 8px; border: 1px solid #d8dee5; border-radius: 4px; color: #4b5563; background: #fff; font: 11px ui-monospace, SFMono-Regular, Menlo, monospace; }.command-row { align-items: stretch; }.command-row .primary-button { flex: 0 0 auto; }.command-output { min-height: 170px; margin: 14px 0 0; padding: 14px; overflow: auto; border-radius: 5px; color: #d1fae5; background: #10231e; font: 12px/1.55 ui-monospace, SFMono-Regular, Menlo, monospace; white-space: pre-wrap; }.history-list > div { display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 12px 0; border-bottom: 1px solid #eef0f2; }.history-list strong { color: #374151; font: 11px ui-monospace, SFMono-Regular, Menlo, monospace; }.history-list small { color: #9ca3af; font-size: 10px; }.terminal-panel { overflow: hidden; border: 1px solid #263342; border-radius: 7px; background: #101820; }.terminal-bar { height: 42px; display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 0 14px; color: #d7e0ea; background: #1b2835; font-size: 12px; }.terminal-bar span { display: flex; gap: 5px; }.terminal-bar i { width: 8px; height: 8px; border-radius: 50%; background: #f87171; }.terminal-bar i:nth-child(2) { background: #fbbf24; }.terminal-bar i:nth-child(3) { background: #34d399; }.terminal-bar a { color: #a7f3d0; text-decoration: none; font-size: 11px; }.terminal-panel iframe { width: 100%; height: 620px; display: block; border: 0; background: #101820; }.mode-value { color: var(--accent-ink) !important; font-size: 21px !important; }.usb-mode-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 10px; }.usb-mode-grid article { display: grid; gap: 8px; min-height: 160px; padding: 16px; border: 1px solid #e2e8f0; border-radius: 6px; }.usb-mode-grid article.selected { border-color: var(--accent); background: var(--accent-soft); }.usb-mode-grid strong { color: #17212b; font-size: 13px; }.usb-mode-grid span { color: #6b7280; font-size: 11px; }.usb-mode-grid .button-group { align-self: end; flex-wrap: wrap; }
.compact-form { margin-bottom: 18px; }.file-button { position: relative; cursor: pointer; }.file-button input { position: absolute; width: 1px; height: 1px; overflow: hidden; opacity: 0; }.danger-text { color: #e11d48 !important; }.update-upload { display: flex; align-items: center; justify-content: space-between; gap: 18px; }.update-upload p { margin-top: 6px; }.feature-card > summary { cursor: pointer; color: #17212b; font-size: 14px; font-weight: 650; }.details-content { display: grid; gap: 12px; margin-top: 16px; }.details-content .secondary-button { justify-self: start; }
.form-grid label.inline-switch { display: flex; flex-direction: row-reverse; align-items: center; justify-content: space-between; min-height: 36px; }.form-grid label.inline-switch input[type="checkbox"] { width: 17px; height: 17px; min-height: 0; flex: 0 0 auto; margin: 0; padding: 0; border: 0; box-shadow: none; accent-color: var(--accent); }
.modem-profile-editor { gap: 0; }.profile-actions { flex-wrap: wrap; justify-content: flex-end; }.profile-actions .file-button { display: inline-flex; align-items: center; justify-content: center; min-height: 36px; padding: 0 13px; border: 1px solid #d1d5db; border-radius: 6px; color: #374151; background: #fff; font-size: 12px; font-weight: 650; }.profile-section { border-top: 1px solid #e7ebef; }.profile-section summary { display: flex; align-items: center; min-height: 54px; cursor: pointer; color: #17212b; font-size: 12px; font-weight: 650; list-style: none; }.profile-section summary::-webkit-details-marker { display: none; }.profile-section summary::after { content: '+'; margin-left: auto; color: #6b7280; font-size: 18px; font-weight: 400; }.profile-section[open] summary::after { content: '-'; }.profile-section-body { padding: 0 0 18px; }.profile-fields { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 14px 18px; }.profile-fields label { min-width: 0; }.profile-footer { justify-content: flex-end; padding-top: 18px; border-top: 1px solid #e7ebef; }
.reboot-days { display: grid; gap: 7px; color: #4b5563; font-size: 11px; font-weight: 650; }.reboot-days > div { display: flex; flex-wrap: wrap; gap: 6px; }.reboot-days button { width: 34px; height: 32px; border: 1px solid #d8dee5; border-radius: 5px; color: #4b5563; background: #fff; font-size: 11px; }.reboot-days button:first-child { width: auto; padding: 0 9px; }.reboot-days button.selected { border-color: var(--accent); color: var(--accent-ink); background: var(--accent-soft); }.reboot-days.disabled { opacity: .5; }.reboot-schedule .button-group { margin-top: 2px; }
.message-list article { cursor: pointer; }.message-list article:focus-visible { outline: 2px solid var(--accent); outline-offset: -2px; }.message-modal-backdrop { position: fixed; z-index: 50; inset: 0; display: grid; place-items: center; padding: 20px; background: rgba(15, 23, 42, .42); }.message-modal { width: min(560px, 100%); max-height: min(680px, calc(100vh - 40px)); overflow: auto; padding: 20px; border: 1px solid #e2e8f0; border-radius: 7px; background: #fff; box-shadow: 0 18px 50px rgba(15, 23, 42, .24); }.modal-close { width: 34px; height: 34px; display: grid; place-items: center; flex: 0 0 auto; border: 0; border-radius: 5px; color: #6b7280; background: transparent; font-size: 17px; }.modal-close:hover, .modal-close:focus-visible { color: #17212b; background: #f1f5f9; }.message-details { display: grid; margin: 0; }.message-details > div { padding: 13px 0; border-top: 1px solid #eef0f2; }.message-details dt { color: #6b7280; font-size: 11px; }.message-details dd { margin: 6px 0 0; color: #17212b; font-size: 13px; overflow-wrap: anywhere; }.message-details .message-body { min-height: 96px; line-height: 1.6; white-space: pre-wrap; }
@media (max-width: 900px) { .two-columns, .sms-layout, .at-layout { grid-template-columns: 1fr; }.metrics-3 { grid-template-columns: repeat(3, minmax(0, 1fr)); }.data-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }.usb-mode-grid { grid-template-columns: 1fr; }.profile-fields { grid-template-columns: 1fr; } }
@media (max-width: 640px) { .feature-card { padding: 16px; }.metrics-3 { grid-template-columns: 1fr; }.data-grid, .data-grid.compact { grid-template-columns: 1fr; }.feature-card-header, .settings-row, .update-upload { align-items: flex-start; flex-direction: column; }.profile-actions, .profile-footer { justify-content: flex-start; }.settings-row .button-group { margin-left: 0; }.data-table { overflow-x: auto; }.table-row { min-width: 620px; }.cell-list { overflow-x: auto; }.cell-list button { min-width: 520px; }.message-list article { grid-template-columns: minmax(0, 1fr) auto; }.message-list article button { grid-column: 2; }.message-modal-backdrop { padding: 12px; }.message-modal { max-height: calc(100vh - 24px); padding: 16px; }.message-modal .feature-card-header { flex-direction: row; align-items: flex-start; }.terminal-panel iframe { height: 480px; } }
</style>
