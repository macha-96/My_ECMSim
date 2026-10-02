<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { useSimulationStore } from '../stores/simulation'

const sim = useSimulationStore()
const chartEl = ref<HTMLDivElement | null>(null)
let chart: echarts.ECharts | null = null
let ro: ResizeObserver | null = null

const SERIES_COLORS = ['var(--radar-color)', '#e94560', '#00e5ff', '#ff7a59', '#69f0ae']

const hasData = computed(() => sim.history.length > 0)

function radarNames(): string[] {
  const first = sim.history[0]
  if (!first) return []
  return first.labels.map(id => `R${id}`)
}

/** 单 ECharts 实例承载上下两图：上 grid SINR(dB)、下 grid ζ(0–1)（每雷达一线） */
function buildOption(): echarts.EChartsOption {
  const names = radarNames()
  const xData = sim.history.map((_, i) => String(i + 1))
  // 逐步骤提取各雷达 SINR / ζ
  const sinrSeries = names.map((_, ri) => sim.history.map(h => h.sinr[ri] ?? null))
  const zetaSeries = names.map((_, ri) => sim.history.map(h => h.zeta[ri] ?? null))
  const axisLabel = { color: 'rgba(255,255,255,.45)' }
  const axisLine = { lineStyle: { color: 'rgba(255,255,255,.18)' } }
  const splitLine = { lineStyle: { color: 'rgba(255,255,255,.08)' } }
  const titleStyle = { color: 'rgba(255,255,255,.62)', fontSize: 11 }
  const lineOpt = (name: string, data: (number | null)[], gridIndex: number, i: number): echarts.LineSeriesOption => ({
    name,
    type: 'line',
    xAxisIndex: gridIndex,
    yAxisIndex: gridIndex,
    smooth: true,
    showSymbol: false,
    lineStyle: { width: 2 },
    itemStyle: { color: SERIES_COLORS[i % SERIES_COLORS.length] },
    data,
  })
  return {
    backgroundColor: 'transparent',
    title: [
      { text: 'SINR (dB)', textStyle: titleStyle, left: 0, top: 0 },
      { text: '频域匹配因子 ζ', textStyle: titleStyle, left: 0, top: '48%' },
    ],
    grid: [
      { left: 44, right: 12, top: 24, height: '36%' },
      { left: 44, right: 12, top: '56%', bottom: 22 },
    ],
    xAxis: [
      { type: 'category', gridIndex: 0, data: xData, axisLabel, axisLine },
      { type: 'category', gridIndex: 1, data: xData, axisLabel, axisLine },
    ],
    yAxis: [
      { type: 'value', gridIndex: 0, axisLabel, splitLine },
      { type: 'value', gridIndex: 1, min: 0, max: 1, axisLabel, splitLine },
    ],
    tooltip: { trigger: 'axis' },
    series: [
      ...sinrSeries.map((data, i) => lineOpt(names[i], data, 0, i)),
      ...zetaSeries.map((data, i) => lineOpt(names[i], data, 1, i)),
    ],
  }
}

function render(): void {
  if (!hasData.value) {
    // 历史被清空 → 释放实例，避免 Vue 卸载 canvas 后残留失效实例
    if (chart) {
      chart.dispose()
      chart = null
    }
    return
  }
  const el = chartEl.value
  if (!el) return
  if (!chart) {
    chart = echarts.init(el)
    ro?.observe(el) // 懒创建晚于挂载时补观察，保证 resize 自适应
  }
  chart.setOption(buildOption())
}

onMounted(() => {
  ro = new ResizeObserver(() => chart?.resize())
  if (chartEl.value) ro.observe(chartEl.value)
  void nextTick(render)
})

// deep 监听历史：容器已挂载时同步 setOption（测试同 tick 内可见）；占位↔图表切换则等 DOM 更新后渲染
watch(() => sim.history, () => {
  if (chartEl.value) render()
  else void nextTick(render)
}, { deep: true })

onBeforeUnmount(() => {
  ro?.disconnect()
  chart?.dispose()
  chart = null
})
</script>

<template>
  <div class="history-charts">
    <div v-if="!hasData" class="placeholder">暂无数据 — 运行仿真后展示</div>
    <div v-else ref="chartEl" class="chart" />
  </div>
</template>

<style scoped>
.history-charts { display: flex; flex-direction: column; height: 100%; gap: 4px; }
.chart { flex: 1; min-height: 280px; }
.placeholder { color: var(--text-2); font-size: 12px; padding: 16px; text-align: center; }
</style>
