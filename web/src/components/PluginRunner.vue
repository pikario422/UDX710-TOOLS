<script setup>
import { createApp, nextTick, onBeforeUnmount, ref, watch } from 'vue'
import { executeShell, getPluginStorage, setPluginStorage, deletePluginStorage } from '../composables/useApi'
import { PluginBtn, PluginCard, PluginStatus } from './plugin'

const props = defineProps({ plugin: { type: Object, required: true } })
const emit = defineEmits(['close'])

const container = ref(null)
const error = ref('')
const output = ref('')
const pluginData = ref({})
let appInstance = null
let pluginDefinition = null
let timers = []

function pluginName() {
  return String(props.plugin.filename || props.plugin.name || '').replace(/\.js$/, '')
}

function clearTimers() {
  timers.forEach(timer => timer.kind === 'interval' ? clearInterval(timer.id) : clearTimeout(timer.id))
  timers = []
}

function destroyRuntime() {
  if (pluginDefinition?.destroyed) {
    try { pluginDefinition.destroyed.call(runtimeContext()) } catch (_) { /* Plugin cleanup must not block closing. */ }
  }
  clearTimers()
  appInstance?.unmount()
  appInstance = null
  pluginDefinition = null
}

function runtimeContext() {
  return { $data: pluginData.value, $api: pluginApi, $refresh: renderTemplate }
}

const pluginApi = {
  async shell(command) {
    try {
      const result = await executeShell(command)
      return result.Code === 0 ? result.Data : result.Error
    } catch (requestError) { return `Error: ${requestError.message}` }
  },
  toast(message) { output.value = String(message) },
  alert(title, message) { output.value = `${title}: ${message}` },
  async confirm(title, message) { return window.confirm(`${title}\n\n${message}`) },
  $setInterval(fn, milliseconds) {
    const id = window.setInterval(fn, milliseconds)
    timers.push({ kind: 'interval', id })
    return id
  },
  $setTimeout(fn, milliseconds) {
    const id = window.setTimeout(fn, milliseconds)
    timers.push({ kind: 'timeout', id })
    return id
  },
  $clearInterval(id) {
    clearInterval(id)
    timers = timers.filter(timer => timer.kind !== 'interval' || timer.id !== id)
  },
  $clearTimeout(id) {
    clearTimeout(id)
    timers = timers.filter(timer => timer.kind !== 'timeout' || timer.id !== id)
  },
  storage: {
    async get(key, defaultValue = null) {
      try {
        const result = await getPluginStorage(pluginName())
        return result.Code === 0 && result.Data && key in result.Data ? result.Data[key] : defaultValue
      } catch (_) { return defaultValue }
    },
    async set(key, value) {
      try {
        const current = await getPluginStorage(pluginName())
        const data = current.Code === 0 && current.Data ? current.Data : {}
        data[key] = value
        return (await setPluginStorage(pluginName(), data)).Code === 0
      } catch (_) { return false }
    },
    async remove(key) {
      try {
        const current = await getPluginStorage(pluginName())
        const data = current.Code === 0 && current.Data ? current.Data : {}
        delete data[key]
        return (await setPluginStorage(pluginName(), data)).Code === 0
      } catch (_) { return false }
    },
    async getAll() {
      try {
        const result = await getPluginStorage(pluginName())
        return result.Code === 0 && result.Data ? result.Data : {}
      } catch (_) { return {} }
    },
    async clear() {
      try { return (await deletePluginStorage(pluginName())).Code === 0 } catch (_) { return false }
    }
  }
}

function renderTemplate() {
  if (!container.value || !pluginDefinition?.template) return
  appInstance?.unmount()
  container.value.replaceChildren()
  const methods = {}
  Object.entries(pluginDefinition.methods || {}).forEach(([name, method]) => {
    methods[name] = (...args) => method.apply(runtimeContext(), args)
  })
  Object.assign(pluginData.value, methods)
  appInstance = createApp({
    components: { PluginBtn, PluginCard, PluginStatus },
    data: () => pluginData.value,
    template: pluginDefinition.template
  })
  appInstance.component('plugin-card', PluginCard)
  appInstance.component('plugin-status', PluginStatus)
  appInstance.component('plugin-btn', PluginBtn)
  appInstance.mount(container.value)
}

async function runPlugin() {
  destroyRuntime()
  error.value = ''
  output.value = ''
  pluginData.value = {}
  await nextTick()
  try {
    const sandboxWindow = { PLUGIN: null }
    const factory = new Function('window', '$api', 'console', `${props.plugin.content || ''}\n; return window.PLUGIN;`)
    pluginDefinition = factory(sandboxWindow, pluginApi, console)
    if (!pluginDefinition?.template) throw new Error('该插件没有提供可打开的页面')
    pluginData.value = typeof pluginDefinition.data === 'function' ? pluginDefinition.data() || {} : {}
    pluginData.value.$api = pluginApi
    renderTemplate()
    if (pluginDefinition.mounted) await pluginDefinition.mounted.call(runtimeContext())
  } catch (runError) {
    error.value = runError.message || '插件页面启动失败'
  }
}

function close() {
  destroyRuntime()
  emit('close')
}

watch(() => props.plugin, runPlugin, { immediate: true, deep: false })
onBeforeUnmount(destroyRuntime)
</script>

<template>
  <div class="plugin-runner-backdrop" role="presentation" @click.self="close">
    <section class="plugin-runner" role="dialog" aria-modal="true" :aria-label="`${plugin.name || plugin.filename} 插件页面`">
      <header class="plugin-runner-header"><div><h3>{{ plugin.name || plugin.filename }}</h3><p>{{ plugin.description || plugin.filename }}</p></div><button type="button" class="plugin-runner-close" title="关闭插件页面" aria-label="关闭插件页面" @click="close"><font-awesome-icon icon="times-circle" /></button></header>
      <div class="plugin-runner-content"><p v-if="error" class="plugin-runner-error">{{ error }}</p><div v-else ref="container" class="plugin-runner-view"></div><pre v-if="output" class="plugin-runner-output">{{ output }}</pre></div>
    </section>
  </div>
</template>

<style scoped>
.plugin-runner-backdrop { position: fixed; z-index: 60; inset: 0; display: grid; place-items: center; padding: 20px; background: rgba(15, 23, 42, .48); }.plugin-runner { width: min(960px, 100%); max-height: min(760px, calc(100vh - 40px)); display: flex; flex-direction: column; overflow: hidden; border: 1px solid #e2e8f0; border-radius: 7px; background: #fff; box-shadow: 0 22px 55px rgba(15, 23, 42, .28); }.plugin-runner-header { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 16px 20px; border-bottom: 1px solid #e7ebef; }.plugin-runner-header h3 { margin: 0; color: #17212b; font-size: 14px; }.plugin-runner-header p { margin: 4px 0 0; color: #6b7280; font-size: 11px; }.plugin-runner-close { width: 34px; height: 34px; display: grid; place-items: center; flex: 0 0 auto; border: 0; border-radius: 5px; color: #6b7280; background: transparent; font-size: 17px; }.plugin-runner-close:hover, .plugin-runner-close:focus-visible { color: #17212b; background: #f1f5f9; }.plugin-runner-content { min-height: 180px; padding: 20px; overflow: auto; }.plugin-runner-error { margin: 0; padding: 12px; border: 1px solid #fecaca; border-radius: 6px; color: #b91c1c; background: #fff1f2; font-size: 12px; }.plugin-runner-output { margin: 16px 0 0; padding: 14px; overflow: auto; border-radius: 5px; color: #d1fae5; background: #10231e; font: 12px/1.55 ui-monospace, SFMono-Regular, Menlo, monospace; white-space: pre-wrap; }
@media (max-width: 640px) { .plugin-runner-backdrop { padding: 12px; }.plugin-runner { max-height: calc(100vh - 24px); }.plugin-runner-header { padding: 14px 16px; }.plugin-runner-content { padding: 16px; } }
</style>
