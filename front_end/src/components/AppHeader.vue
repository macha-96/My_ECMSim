<script setup lang="ts">
import { computed, watch, ref, onBeforeUnmount } from 'vue'
import { useSceneStore } from '../stores/scene'
import { ElMessage } from 'element-plus'
import { listScenes } from '../api/scene'
import { errText } from '../api/http'
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

const popoverVisible = ref(false)
const sessions = ref<any[]>([])
const refreshing = ref(false)
const dropdownTop = ref(0)
const dropdownLeft = ref(0)
let refreshTimer: ReturnType<typeof setTimeout> | null = null

const refreshSessions = async (): Promise<void> => {
  refreshing.value = true
  try {
    const r = await listScenes()
    if (r.success && r.sessions) {
      sessions.value = r.sessions
    } else {
      ElMessage.error(r.error || '获取会话列表失败')
    }
  } catch (e) {
    ElMessage.error(errText(e))
  } finally {
    refreshing.value = false
    if (popoverVisible.value) {
      refreshTimer = setTimeout(refreshSessions, 10000)
    }
  }
}

const cancelRefresh = (): void => {
  if (refreshTimer) {
    clearTimeout(refreshTimer)
    refreshTimer = null
  }
}

const handleSwitchSession = async (sid: string): Promise<void> => {
  if (!sid || sid === scene.sessionId) return
  scene.sessionId = sid
  await scene.fetchScene()
  popoverVisible.value = false
}

const getRelativeTime = (ts: number): string => {
  const diff = Math.max(0, Math.floor(Date.now() / 1000) - ts)
  if (diff < 60) return `${diff} 秒前`
  if (diff < 3600) return `${Math.floor(diff / 60)} 分钟前`
  if (diff < 86400) return `${Math.floor(diff / 3600)} 小时前`
  return `${Math.floor(diff / 86400)} 天前`
}

function onWrapperEnter(e: MouseEvent): void {
  const rect = (e.currentTarget as HTMLElement).getBoundingClientRect()
  dropdownTop.value = rect.bottom + 4
  dropdownLeft.value = rect.right - 320
  popoverVisible.value = true
  void refreshSessions()
}

function onDropdownLeave(): void {
  popoverVisible.value = false
}

function keepOpen(): void {
  popoverVisible.value = true
}

watch(
  () => popoverVisible.value,
  visible => {
    if (visible) {
      void refreshSessions()
    } else {
      cancelRefresh()
    }
  }
)

onBeforeUnmount(() => {
  cancelRefresh()
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
      <div
        class="session-wrapper"
        @mouseenter="onWrapperEnter"
        @mouseleave="popoverVisible = false"
      >
        <span class="badge sid mono" :title="props.sessionId ?? ''">
          会话 {{ props.sessionId ?? '—' }}
        </span>
        <Teleport to="body">
          <Transition name="fade">
            <div
              v-if="popoverVisible"
              class="popover-dropdown"
              :style="{ top: dropdownTop + 'px', left: dropdownLeft + 'px' }"
              @mouseenter="keepOpen"
              @mouseleave="onDropdownLeave"
            >
              <div class="popover-head">
                <span>全部会话</span>
                <button
                  class="refresh-btn"
                  :disabled="refreshing"
                  @click="refreshSessions"
                >
                  {{ refreshing ? '刷新中…' : '刷新' }}
                </button>
              </div>
              <div v-if="sessions.length === 0 && !refreshing" class="empty">暂无会话</div>
              <ul v-else class="popover-list">
                <li
                  v-for="s in sessions"
                  :key="s.session_id"
                  class="popover-item"
                  :class="{ current: s.session_id === scene.sessionId }"
                  @click="handleSwitchSession(s.session_id)"
                >
                  <div class="row1">
                    <span class="sid mono" :title="s.session_id">{{ s.session_id }}</span>
                    <span v-if="s.session_id === scene.sessionId" class="badge">当前</span>
                  </div>
                  <div class="row2">
                    <span>雷达 {{ s.radar_count }} · 干扰机 {{ s.jammer_count }}</span>
                    <span>{{ getRelativeTime(s.last_access) }}</span>
                    <span v-if="s.ws_connections > 0" class="ws">在线 {{ s.ws_connections }}</span>
                  </div>
                </li>
              </ul>
            </div>
          </Transition>
        </Teleport>
      </div>
    </div>
    <span v-if="scene.dirtyCount > 0" class="badge pending">待同步 {{ scene.dirtyCount }}</span>
    <span v-else-if="scene.syncing" class="badge syncing">同步中</span>
    <span v-else class="badge synced">已同步</span>
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
.badges {
  display: flex;
  gap: 8px;
  align-items: center;
}
.badge {
  font-size: 12px;
  padding: 3px 10px;
  border-radius: 999px;
  border: 1px solid var(--glass-border);
  background: rgba(0, 0, 0, 0.25);
  cursor: pointer;
}
.badge.ws .dot {
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

.session-wrapper {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  cursor: pointer;
}

.popover-dropdown {
  position: fixed;
  width: 320px;
  background: var(--background);
  border: 1px solid var(--glass-border);
  border-radius: 8px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
  z-index: 9999;
  padding: 8px 0;
  font-size: 12px;
}
.popover-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
  padding: 0 12px;
}
.popover-head span { color: var(--text-2); font-size: 12px; }
.refresh-btn {
  background: none;
  border: none;
  color: var(--cyan);
  font-size: 11px;
  cursor: pointer;
  padding: 0;
}
.refresh-btn:disabled { opacity: .5; cursor: default; }
.empty { color: var(--text-2); font-size: 12px; text-align: center; padding: 8px 0; }
.popover-list {
  list-style: none;
  margin: 0;
  padding: 0;
  max-height: 260px;
  overflow: auto;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.popover-item {
  padding: 6px 8px;
  border-radius: 6px;
  border: 1px solid transparent;
  cursor: pointer;
}
.popover-item:hover { background: rgba(255, 255, 255, .06); }
.popover-item.current {
  border-color: var(--accent);
  background: rgba(233, 69, 96, .12);
  cursor: default;
}
.row1 { display: flex; align-items: center; gap: 6px; }
.sid { font-size: 12px; color: var(--text-1); overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.badge.small { font-size: 10px; color: var(--accent); border: 1px solid var(--accent); border-radius: 4px; padding: 0 4px; }
.row2 { display: flex; gap: 10px; font-size: 11px; color: var(--text-2); margin-top: 2px; }
.ws { color: var(--ok); }

.fade-enter-active,
.fade-leave-active {
  transition: opacity .2s ease;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
