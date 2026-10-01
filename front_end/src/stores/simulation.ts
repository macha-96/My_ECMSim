import { ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { ElMessage } from 'element-plus'
import type { HistStep, SimResult } from '../types/api'
import * as api from '../api/scene'
import { errText } from '../api/http'
import { useSceneStore } from './scene'

export const MAX_HISTORY = 30

function toStep(res: SimResult[]): HistStep {
  return {
    labels: res.map(r => r.radar_id),
    sinr: res.map(r => r.SINR_dB),
    zeta: res.map(r => (r.jammer_details?.length
      ? r.jammer_details.reduce((s, j) => s + (j.freq_match_factor || 0), 0) / r.jammer_details.length
      : 0)),
  }
}

export const useSimulationStore = defineStore('simulation', () => {
  const results = ref<SimResult[] | null>(null)
  const history = ref<HistStep[]>([])
  const running = ref(false)
  const statusMsg = ref('就绪')
  const auto = ref({ enabled: false, interval: 5, remaining: 0 })

  const scene = useSceneStore()
  let autoTimer: ReturnType<typeof setInterval> | null = null

  function clearResults(): void {
    results.value = null
    history.value = []
  }

  /** 运行一次仿真；永不抛异常，失败返回 false（running 必回 false） */
  async function run(): Promise<boolean> {
    if (!scene.sessionId || running.value) return false
    running.value = true
    statusMsg.value = '仿真中...'
    try {
      await scene.flushSync()
      const r = await api.simulate(scene.sessionId)
      if (!r.success || !r.sim_results) throw new Error(r.error || '仿真失败')
      results.value = r.sim_results
      history.value = [...history.value, toStep(r.sim_results)].slice(-MAX_HISTORY)
      statusMsg.value = `步骤 ${history.value.length}`
      return true
    } catch (e) {
      statusMsg.value = '仿真失败'
      ElMessage.error(errText(e))
      return false
    } finally {
      running.value = false
    }
  }

  function startAuto(): void {
    if (auto.value.enabled) return
    const sec = Math.max(1, Math.floor(auto.value.interval))
    auto.value.interval = sec
    auto.value.enabled = true
    auto.value.remaining = sec
    autoTimer = setInterval(() => {
      auto.value.remaining -= 1
      if (auto.value.remaining <= 0) {
        auto.value.remaining = auto.value.interval // 每次触发重读最新间隔
        void run()
      }
    }, 1000)
  }

  function stopAuto(): void {
    if (autoTimer) clearInterval(autoTimer)
    autoTimer = null
    auto.value.enabled = false
    auto.value.remaining = 0
  }

  async function reset(): Promise<void> {
    stopAuto()
    try {
      if (scene.sessionId) await api.deleteScene(scene.sessionId)
      await scene.initSession()
      await scene.fetchScene()
    } catch (e) {
      ElMessage.error(errText(e))
    }
    clearResults()
    statusMsg.value = '已重置'
  }

  // 实体增删（ID 签名变化）→ 清空结果与历史，避免读到过期数据
  watch(
    () => [
      ...scene.radars.map(r => r.id).sort((a, b) => a - b),
      '|',
      ...scene.jammers.map(j => j.id).sort((a, b) => a - b),
    ].join(','),
    () => clearResults(),
  )

  return { results, history, running, statusMsg, auto, run, reset, clearResults, startAuto, stopAuto }
})
