<script setup lang="ts">
import { computed, onBeforeUnmount, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
// 局部注册表单组件：测试环境未挂载 app.use(ElementPlus)，且 spec 用 vi.mock 截获
// element-plus 根入口 —— 走子路径导入不受 mock 影响，保证测试与应用渲染一致
import ElSlider from 'element-plus/es/components/slider/index'
import { ElOption, ElSelect } from 'element-plus/es/components/select/index'
import type { EntityKind, Jammer } from '../types/api'
import { pendingKey, useSceneStore } from '../stores/scene'

const props = defineProps<{ kind: EntityKind; id: number }>()
const scene = useSceneStore()

const entity = computed(() =>
  props.kind === 'radar'
    ? scene.radars.find(r => r.id === props.id)
    : scene.jammers.find(j => j.id === props.id),
)

interface FieldSpec {
  prop: string
  label: string
  min: number
  max: number
  step: number
  /** 存储值换算为显示值 */
  toDisplay?: (v: number) => number
  /** 显示值换算回存储值 */
  toStorage?: (v: number) => number
  unit?: string
}

const radarFields: FieldSpec[] = [
  { prop: 'x', label: 'X', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'y', label: 'Y', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'Pt_dBm', label: '功率', min: -20, max: 60, step: 1, unit: 'dBm' },
  { prop: 'G_dB', label: '增益', min: 0, max: 40, step: 1, unit: 'dB' },
  { prop: 'freq', label: '频率', min: 8, max: 18, step: 0.001, unit: 'GHz', toDisplay: v => v / 1e9, toStorage: v => v * 1e9 },
  { prop: 'bandwidth', label: '带宽', min: 1, max: 100, step: 1, unit: 'MHz', toDisplay: v => v / 1e6, toStorage: v => v * 1e6 },
  { prop: 'sigma', label: 'RCS', min: 0.01, max: 100, step: 0.01, unit: 'm²' },
  { prop: 'thresh_db', label: '检测门限', min: -80, max: -30, step: 1, unit: 'dB' },
]

const jammerFields: FieldSpec[] = [
  { prop: 'x', label: 'X', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'y', label: 'Y', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'Pj_dBm', label: '功率', min: 0, max: 80, step: 1, unit: 'dBm' },
  { prop: 'Gj_dB', label: '增益', min: 0, max: 40, step: 1, unit: 'dB' },
  { prop: 'jam_freq', label: '干扰频率', min: 8, max: 18, step: 0.001, unit: 'GHz', toDisplay: v => v / 1e9, toStorage: v => v * 1e9 },
]

const fields = computed(() => (props.kind === 'radar' ? radarFields : jammerFields))

function numOf(prop: string): number {
  const raw = (entity.value as unknown as Record<string, number>)[prop]
  return raw
}

function displayVal(f: FieldSpec): number {
  return f.toDisplay ? f.toDisplay(numOf(f.prop)) : numOf(f.prop)
}

function onInput(f: FieldSpec, displayValue: number): void {
  const stored = f.toStorage ? f.toStorage(displayValue) : displayValue
  scene.updateField(props.kind, props.id, f.prop, stored)
}

function keyOf(prop: string): string {
  return pendingKey(props.kind, prop, props.id)
}

function isPending(prop: string): boolean {
  return scene.pendingEdits.has(keyOf(prop))
}

function onFocus(prop: string): void {
  scene.setFocusedField(keyOf(prop))
}
function onBlur(): void {
  scene.setFocusedField(null)
}

async function onDelete(): Promise<void> {
  if (!entity.value) return
  try {
    await ElMessageBox.confirm(`确认删除 ${props.kind === 'radar' ? 'R' : 'J'}${props.id}？`, '删除', { type: 'warning' })
  } catch {
    return // 用户取消
  }
  if (props.kind === 'radar') await scene.removeRadar(props.id)
  else await scene.removeJammer(props.id)
  // 删除成功后 store 会移除实体；失败时 store 已弹错误提示，此处不再重复
  if (!entity.value) ElMessage.success(`已删除 ${props.kind === 'radar' ? 'R' : 'J'}${props.id}`)
}

const pulsing = computed(() =>
  scene.pulsed && scene.pulsed.kind === props.kind && scene.pulsed.id === props.id &&
  Date.now() - scene.pulsed.at < 600,
)
// 一次性高亮：到期清除 pulsed，避免同一脉冲残留；卸载时取消定时器
let pulseTimer: ReturnType<typeof setTimeout> | null = null
watch(pulsing, v => {
  if (v && !pulseTimer) {
    pulseTimer = setTimeout(() => {
      pulseTimer = null
      scene.pulsed = null
    }, 650)
  }
})
onBeforeUnmount(() => {
  if (pulseTimer) clearTimeout(pulseTimer)
})
</script>

<template>
  <div v-if="entity" class="entity-card glass-panel" :class="{ pulsing }">
    <div class="card-head">
      <span class="name mono">
        {{ kind === 'radar' ? 'R' : 'J' }}{{ id }}
        <em>{{ kind === 'radar' ? '雷达' : '干扰机' }}</em>
      </span>
      <button class="del" title="删除" @click="onDelete">✕</button>
    </div>
    <!-- el-slider 不透传 focus/blur：在字段容器上用捕获阶段监听，保证 focusedKey 契约 -->
    <div
      v-for="f in fields"
      :key="f.prop"
      class="field"
      :data-prop="f.prop"
      :class="{ pending: isPending(f.prop) }"
      @focus.capture="onFocus(f.prop)"
      @blur.capture="onBlur"
    >
      <label>
        {{ f.label }}
        <span v-if="isPending(f.prop)" class="tilde">~</span>
      </label>
      <el-slider
        :model-value="displayVal(f)"
        :min="f.min"
        :max="f.max"
        :step="f.step"
        show-input
        size="small"
        @update:model-value="onInput(f, $event as number)"
      />
      <span class="val mono">{{ f.unit }}</span>
    </div>
    <div
      v-if="kind === 'jammer'"
      class="field"
      data-prop="jam_type"
      :class="{ pending: isPending('jam_type') }"
      @focus.capture="onFocus('jam_type')"
      @blur.capture="onBlur"
    >
      <label>
        类型
        <span v-if="isPending('jam_type')" class="tilde">~</span>
      </label>
      <el-select
        :model-value="(entity as Jammer).jam_type"
        size="small"
        @update:model-value="scene.updateField('jammer', id, 'jam_type', $event)"
      >
        <el-option label="噪声压制" value="NOISE_JAM" />
        <el-option label="距离欺骗" value="RANGE_DECEPT" />
      </el-select>
    </div>
  </div>
</template>

<style scoped>
.entity-card {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 12px;
}

.card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.name {
  font-size: 14px;
  color: var(--text-1);
}

.name em {
  font-style: normal;
  font-size: 11px;
  color: var(--text-2);
  margin-left: 6px;
}

.del {
  background: transparent;
  border: none;
  color: var(--text-2);
  font-size: 13px;
  padding: 2px 6px;
  border-radius: 4px;
  cursor: pointer;
}

.del:hover {
  color: var(--danger);
}

.field {
  display: grid;
  grid-template-columns: 64px minmax(0, 1fr) auto;
  align-items: center;
  gap: 8px;
}

.field label {
  font-size: 12px;
  color: var(--text-2);
}

.field.pending label {
  color: var(--accent);
}

.tilde {
  color: var(--accent);
  font-weight: 600;
  margin-left: 2px;
}

.val {
  font-size: 11px;
  color: var(--text-2);
  min-width: 34px;
}

.field .el-select {
  width: 100%;
}

.pulsing {
  animation: card-pulse 0.6s ease;
}

@keyframes card-pulse {
  0% {
    box-shadow: 0 0 0 0 rgba(233, 69, 96, 0.6);
  }

  100% {
    box-shadow: 0 0 0 12px rgba(233, 69, 96, 0);
  }
}
</style>
