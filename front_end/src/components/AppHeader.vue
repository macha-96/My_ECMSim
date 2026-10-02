<script setup lang="ts">
import { computed } from 'vue'
import { useSceneStore } from '../stores/scene'
import type { WsStatus } from '../composables/useWebSocket'

const props = defineProps<{ wsStatus: WsStatus; sessionId: string | null }>()

const scene = useSceneStore()

const wsMeta = computed(() => {
  const m: Record<WsStatus, { label: string; color: string }> = {
    connected: { label: '已连接', color: 'var(--ok)' },
    connecting: { label: '连接中', color: 'var(--cyan)' },
    reconnecting: { label: '重连中', color: 'var(--accent)' },
    disconnected: { label: '已断开', color: 'var(--danger)' },
  }
  return m[props.wsStatus]
})
</script>

<template>
  <header class="header glass-panel">
    <div class="brand">
      <span class="logo">ECM</span>
      <span class="title">电子对抗仿真平台</span>
    </div>
    <div class="badges">
      <span class="badge ws mono" :style="{ '--dot': wsMeta.color }">
        <i class="dot" />{{ wsMeta.label }}
      </span>
      <span class="badge sid mono" :title="sessionId ?? ''">
        会话 {{ sessionId ?? '—' }}
      </span>
      <span v-if="scene.dirtyCount > 0" class="badge pending">待同步 {{ scene.dirtyCount }}</span>
      <span v-else-if="scene.syncing" class="badge syncing">同步中</span>
      <span v-else class="badge synced">已同步</span>
    </div>
  </header>
</template>

<style scoped>
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  margin: 8px;
}
.brand { display: flex; align-items: baseline; gap: 10px; }
.logo {
  font-weight: 800;
  letter-spacing: 2px;
  background: linear-gradient(90deg, var(--accent), var(--accent-2));
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}
.title { color: var(--text-2); font-size: 13px; }
.badges { display: flex; gap: 8px; align-items: center; }
.badge {
  font-size: 12px;
  padding: 3px 10px;
  border-radius: 999px;
  border: 1px solid var(--glass-border);
  background: rgba(0, 0, 0, 0.25);
}
.ws .dot {
  display: inline-block;
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--dot);
  margin-right: 6px;
}
.pending { color: var(--accent); border-color: var(--accent); }
.syncing { color: var(--cyan); border-color: var(--cyan); }
.synced { color: var(--ok); border-color: rgba(105, 240, 174, 0.4); }
</style>
