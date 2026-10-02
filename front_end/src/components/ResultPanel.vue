<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useSimulationStore } from '../stores/simulation'
import { useSceneStore } from '../stores/scene'
import JammerDetailTable from './JammerDetailTable.vue'
// 局部注册表格/表单组件：测试环境未挂载 app.use(ElementPlus)，且 spec 用 vi.mock 截获
// element-plus 根入口 —— 走子路径导入不受 mock 影响，保证测试与应用渲染一致
import { ElTable, ElTableColumn } from 'element-plus/es/components/table/index'
import type { TableInstance } from 'element-plus/es/components/table/index'
import ElSwitch from 'element-plus/es/components/switch/index'
import ElInputNumber from 'element-plus/es/components/input-number/index'
import ElTag from 'element-plus/es/components/tag/index'
import type { SimResult } from '../types/api'

const sim = useSimulationStore()
const scene = useSceneStore()

const runDisabled = computed(() => sim.running || !scene.sessionId)

function sinrClass(r: SimResult): string {
  return r.SINR_dB >= 0 ? 'ok' : 'bad'
}

function fmt(v: number | undefined, digits = 2): string {
  return typeof v === 'number' ? v.toFixed(digits) : '—'
}

const tableRef = ref<TableInstance | null>(null)

// Element Plus 列注册分两步：各 el-table-column 的 mounted 仅写入 _columns，
// 表格自身 mounted 里 await nextTick() 后才 updateColumns() 生成渲染用 columns。
// 挂载时同步补跑第二步，让首个渲染周期就输出行内容（否则要多等一个 tick 才可见）。
onMounted(() => {
  tableRef.value?.store?.updateColumns?.()
})
</script>

<template>
  <div class="result-panel">
    <div class="controls">
      <button class="btn primary" :disabled="runDisabled" :class="{ loading: sim.running }" @click="sim.run()">
        运行仿真
      </button>
      <button class="btn" @click="sim.reset()">重置场景</button>
      <label class="auto">
        自动仿真
        <el-switch
          :model-value="sim.auto.enabled"
          @update:model-value="(v: string | number | boolean) => (v ? sim.startAuto() : sim.stopAuto())"
        />
        <el-input-number
          v-model="sim.auto.interval"
          :min="1"
          :max="60"
          size="small"
          :disabled="sim.auto.enabled"
        />
        <span v-if="sim.auto.enabled" class="countdown mono">剩余 {{ sim.auto.remaining }} s</span>
      </label>
    </div>

    <div v-if="!sim.results" class="empty glass-panel">尚未运行仿真</div>

    <el-table ref="tableRef" v-else :data="sim.results" size="small" class="result-table">
      <el-table-column type="expand">
        <template #default="{ row }">
          <JammerDetailTable :details="row.jammer_details ?? []" />
        </template>
      </el-table-column>
      <el-table-column label="雷达" width="70">
        <template #default="{ row }"><span class="mono">R{{ row.radar_id }}</span></template>
      </el-table-column>
      <el-table-column label="SINR(dB)" width="90">
        <template #default="{ row }">
          <span class="mono" :class="sinrClass(row as SimResult)">{{ fmt(row.SINR_dB, 1) }}</span>
        </template>
      </el-table-column>
      <el-table-column label="JSR(dB)" width="86">
        <template #default="{ row }"><span class="mono">{{ fmt(row.jsr_dB, 1) }}</span></template>
      </el-table-column>
      <el-table-column label="检测" width="80">
        <template #default="{ row }">
          <el-tag :type="row.detect_success ? 'danger' : 'success'" size="small">
            {{ row.detect_success ? '检出' : '未检出' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="总压制功率(W)" width="130">
        <template #default="{ row }"><span class="mono">{{ fmt(row.total_effective_jam_power_W, 3) }}</span></template>
      </el-table-column>
      <el-table-column label="压制得分" width="90">
        <template #default="{ row }"><span class="mono">{{ fmt(row.jam_success_score) }}</span></template>
      </el-table-column>
      <el-table-column label="欺骗" width="80">
        <template #default="{ row }">
          <el-tag v-if="row.is_deception_active" type="warning" size="small">激活</el-tag>
          <span v-else class="dim">—</span>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<style scoped>
.result-panel { display: flex; flex-direction: column; gap: 8px; padding: 8px; height: 100%; overflow: auto; }
.controls { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.btn {
  background: rgba(255,255,255,.08);
  border: 1px solid var(--glass-border);
  color: var(--text-1);
  font-size: 12px;
  padding: 6px 14px;
  border-radius: 6px;
  cursor: pointer;
}
.btn.primary { background: linear-gradient(90deg, var(--accent), var(--accent-2)); border: none; }
.btn:disabled { opacity: .45; cursor: not-allowed; }
.btn.loading { animation: btn-pulse 1s ease-in-out infinite; }
@keyframes btn-pulse { 50% { opacity: .55; } }
.auto { display: flex; gap: 8px; align-items: center; font-size: 12px; color: var(--text-2); }
.countdown { color: var(--cyan); font-size: 12px; }
.empty { color: var(--text-2); font-size: 13px; text-align: center; padding: 28px 0; }
.ok { color: var(--ok); }
.bad { color: var(--danger); }
.dim { color: var(--text-2); }
.result-table { --el-table-bg-color: transparent; --el-table-tr-bg-color: transparent; }
</style>
