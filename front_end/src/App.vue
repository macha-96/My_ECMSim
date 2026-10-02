<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import AppHeader from './components/AppHeader.vue'
import StatusBar from './components/StatusBar.vue'
import SceneCanvas from './components/SceneCanvas.vue'
import ControlPanel from './components/ControlPanel.vue'
import ResultPanel from './components/ResultPanel.vue'
import HistoryCharts from './components/HistoryCharts.vue'
import BottomDrawer from './components/BottomDrawer.vue'
import SplitBar from './components/SplitBar.vue'
import { useSceneStore } from './stores/scene'
import { createWebSocketController, type WsStatus } from './composables/useWebSocket'
import { errText } from './api/http'
import type { Jammer, Radar } from './types/api'

const scene = useSceneStore()
const wsStatus = ref<WsStatus>('connecting')

const ws = createWebSocketController({
  onMessage: data => {
    let msg: { radars?: Radar[]; jammers?: Jammer[] }
    try {
      msg = JSON.parse(data)
    } catch {
      return // 非 JSON 心跳等消息忽略
    }
    if (msg && (msg.radars || msg.jammers)) scene.applyRemoteScene(msg)
  },
  onStatus: s => {
    wsStatus.value = s
  },
})

const connectedSid = ref<string | null>(null)

function connectWs(sid: string): void {
  if (connectedSid.value === sid) return
  connectedSid.value = sid
  ws.connect(sid)
}

// reset() 会删除旧 session 并 initSession 出新 sessionId：socket 必须跟随切换，
// 否则停留在已删除 session 上收不到广播。connect() 内部已干净关闭旧 socket。
watch(
  () => scene.sessionId,
  sid => {
    if (sid) connectWs(sid)
  },
)
const canvasHeight = ref(560)

function measureCanvas(): void {
  // main 区高度 - 抽屉高度(约) → 由 CSS flex 自然处理，canvas 高度取其容器实测
  const el = document.querySelector('.stage') as HTMLElement | null
  if (el) canvasHeight.value = Math.max(240, el.clientHeight - 16)
}

onMounted(async () => {
  try {
    await scene.initSession()
    await scene.fetchScene()
    if (scene.sessionId) connectWs(scene.sessionId)
  } catch (e) {
    ElMessage.error(errText(e))
  }
  measureCanvas()
  window.addEventListener('resize', measureCanvas)
})

onBeforeUnmount(() => window.removeEventListener('resize', measureCanvas))
</script>

<template>
  <div class="app-shell">
    <AppHeader :ws-status="wsStatus" :session-id="scene.sessionId" />
    <main class="main">
      <SplitBar
        direction="horizontal"
        fixed-pane="b"
        :min-a="320"
        :min-b="240"
        :max-fixed="480"
        storage-key="ecmsim.sideW"
        :default-value="300"
      >
        <template #a>
          <section class="stage glass-panel">
            <SceneCanvas :height="canvasHeight" />
          </section>
        </template>
        <template #b>
          <aside class="side glass-panel">
            <ControlPanel />
          </aside>
        </template>
      </SplitBar>
    </main>
    <BottomDrawer>
      <template #left><ResultPanel /></template>
      <template #right><HistoryCharts /></template>
    </BottomDrawer>
    <StatusBar />
  </div>
</template>

<style scoped>
.main {
  display: flex;
  gap: 8px;
  padding: 0 8px;
  min-height: 0;
}
.stage { flex: 1; min-width: 0; overflow: hidden; }
.side { flex-shrink: 0; overflow: hidden; display: flex; flex-direction: column; }
</style>
