<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import AppHeader from './components/AppHeader.vue'
import StatusBar from './components/StatusBar.vue'
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

onMounted(async () => {
  try {
    await scene.initSession()
    await scene.fetchScene()
    if (scene.sessionId) ws.connect(scene.sessionId)
  } catch (e) {
    ElMessage.error(errText(e))
  }
})
</script>

<template>
  <div class="app-shell">
    <AppHeader :ws-status="wsStatus" :session-id="scene.sessionId" />
    <main class="main">
      <div class="stage glass-panel">
        <!-- Task 11–15 逐步填充：画布 / 控制面板 / 抽屉 -->
        <p class="placeholder">场景画布（Task 11 接入）</p>
      </div>
    </main>
    <StatusBar />
  </div>
</template>

<style scoped>
.stage {
  flex: 1;
  min-height: 0;
  margin: 0 8px;
  display: grid;
  place-items: center;
}
.placeholder {
  color: var(--text-2);
  font-size: 13px;
}
</style>
