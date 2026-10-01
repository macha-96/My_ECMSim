import { computed, ref } from 'vue'
import { defineStore } from 'pinia'
import { ElMessage } from 'element-plus'
import type { EntityKind, Jammer, Radar } from '../types/api'
import * as api from '../api/scene'
import { errText } from '../api/http'

/** 统一 pending 键：`Pt_dBm_1` / `x_j2` / `jam_type_j3` */
export function pendingKey(kind: EntityKind, prop: string, id: number): string {
  return `${prop}_${kind === 'jammer' ? 'j' : ''}${id}`
}

/** pending 键取实体后缀：`j3` → 干扰机 3，`1` → 雷达 1（prop 可能含下划线，取最后一个 _ 之后） */
function suffixOf(key: string): string {
  return key.slice(key.lastIndexOf('_') + 1)
}

export const useSceneStore = defineStore('scene', () => {
  const sessionId = ref<string | null>(null)
  const radars = ref<Radar[]>([])
  const jammers = ref<Jammer[]>([])
  const filter = ref<'all' | 'radar' | 'jammer'>('all')
  const pendingEdits = ref<Set<string>>(new Set())
  const focusedKey = ref<string | null>(null)
  const syncing = ref(false)
  const lastEdited = ref<{ kind: EntityKind; id: number; at: number } | null>(null)
  const pulsed = ref<{ kind: EntityKind; id: number; at: number } | null>(null)

  const dirtyCount = computed(() => pendingEdits.value.size)

  async function initSession(): Promise<void> {
    const r = await api.createSession()
    if (!r.success || !r.session_id) throw new Error(r.error || '创建会话失败')
    sessionId.value = r.session_id
  }

  async function fetchScene(): Promise<void> {
    if (!sessionId.value) return
    const r = await api.fetchSceneConfig(sessionId.value)
    if (!r.success) throw new Error(r.error || '获取场景失败')
    radars.value = r.radars ?? []
    jammers.value = r.jammers ?? []
  }

  function nextId(list: { id: number }[]): number {
    return list.length ? Math.max(...list.map(e => e.id)) + 1 : 1
  }

  async function addRadar(): Promise<void> {
    if (!sessionId.value) return
    const radar: Radar = {
      id: nextId(radars.value), x: 5000, y: 5000,
      Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50,
    }
    try {
      const r = await api.addRadar(sessionId.value, radar)
      if (r.success) radars.value.push(radar)
      else ElMessage.error(r.error || '添加雷达失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function addJammer(): Promise<void> {
    if (!sessionId.value) return
    const jammer: Jammer = {
      id: nextId(jammers.value), x: 6000, y: 6000,
      Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM',
    }
    try {
      const r = await api.addJammer(sessionId.value, jammer)
      if (r.success) jammers.value.push(jammer)
      else ElMessage.error(r.error || '添加干扰机失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function removeRadar(id: number): Promise<void> {
    if (!sessionId.value) return
    try {
      const r = await api.deleteRadar(sessionId.value, id)
      if (r.success) {
        radars.value = radars.value.filter(e => e.id !== id)
        for (const k of [...pendingEdits.value]) if (suffixOf(k) === String(id)) pendingEdits.value.delete(k)
      } else ElMessage.error(r.error || '删除雷达失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function removeJammer(id: number): Promise<void> {
    if (!sessionId.value) return
    try {
      const r = await api.deleteJammer(sessionId.value, id)
      if (r.success) {
        jammers.value = jammers.value.filter(e => e.id !== id)
        for (const k of [...pendingEdits.value]) if (suffixOf(k) === `j${id}`) pendingEdits.value.delete(k)
      } else ElMessage.error(r.error || '删除干扰机失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  /** 所有参数写入的唯一入口：本地立即生效 + 标记 pending + 触发防抖上送 */
  function updateField(kind: EntityKind, id: number, prop: string, value: number | string): void {
    const list = kind === 'radar' ? radars.value : jammers.value
    const ent = list.find(e => e.id === id)
    if (!ent) return
    ;(ent as unknown as Record<string, number | string>)[prop] = value
    pendingEdits.value.add(pendingKey(kind, prop, id))
    lastEdited.value = { kind, id, at: Date.now() }
    scheduleSync()
  }

  /** 拖拽结束后给对应实体卡片一次高亮脉冲 */
  function pulseEntity(kind: EntityKind, id: number): void {
    pulsed.value = { kind, id, at: Date.now() }
  }

  let syncTimer: ReturnType<typeof setTimeout> | null = null

  /** 300ms 防抖：滑杆连续拖动只触发一次按实体批量上送 */
  function scheduleSync(): void {
    if (syncTimer) clearTimeout(syncTimer)
    syncTimer = setTimeout(() => {
      syncTimer = null
      void flushSync()
    }, 300)
  }

  /** 按实体分组上送 pending 字段；成功清该实体 pending，失败保留并提示 */
  async function flushSync(): Promise<boolean> {
    if (!sessionId.value || pendingEdits.value.size === 0) return true
    syncing.value = true
    try {
      let allOk = true
      for (const r of radars.value) {
        const idStr = String(r.id)
        const keys = [...pendingEdits.value].filter(k => suffixOf(k) === idStr)
        if (keys.length === 0) continue
        try {
          const resp = await api.updateRadar(sessionId.value, r)
          if (resp.success) {
            for (const k of keys) pendingEdits.value.delete(k)
          } else {
            allOk = false
            ElMessage.error(resp.error || `雷达 R${r.id} 更新失败`)
          }
        } catch (e) {
          allOk = false
          ElMessage.error(errText(e))
        }
      }
      for (const j of jammers.value) {
        const suffix = `j${j.id}`
        const keys = [...pendingEdits.value].filter(k => suffixOf(k) === suffix)
        if (keys.length === 0) continue
        try {
          const resp = await api.updateJammer(sessionId.value, j)
          if (resp.success) {
            for (const k of keys) pendingEdits.value.delete(k)
          } else {
            allOk = false
            ElMessage.error(resp.error || `干扰机 J${j.id} 更新失败`)
          }
        } catch (e) {
          allOk = false
          ElMessage.error(errText(e))
        }
      }
      return allOk
    } finally {
      syncing.value = false
    }
  }
  // —— 占位：Task 5 用真实实现替换 applyRemoteScene/setFocusedField ——
  function setFocusedField(_key: string | null): void {
    /* Task 5 覆盖 */
  }
  function applyRemoteScene(_msg: { radars?: Radar[]; jammers?: Jammer[] }): void {
    /* Task 5 覆盖 */
  }

  return {
    sessionId, radars, jammers, filter, pendingEdits, focusedKey, syncing, lastEdited, pulsed,
    dirtyCount,
    initSession, fetchScene, addRadar, addJammer, removeRadar, removeJammer,
    updateField, pulseEntity, scheduleSync, flushSync, applyRemoteScene, setFocusedField,
  }
})
