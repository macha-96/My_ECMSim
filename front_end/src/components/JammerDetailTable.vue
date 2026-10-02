<script setup lang="ts">
import type { JammerDetail } from '../types/api'

defineProps<{ details: JammerDetail[] }>()

function mhz(hz: number): string {
  return (hz / 1e6).toFixed(2)
}
function mw(w: number): string {
  return (w * 1000).toFixed(1)
}
function zeta(v: number): string {
  return v.toFixed(2)
}
</script>

<template>
  <div class="detail">
    <div v-if="details.length === 0" class="empty">无干扰机参与</div>
    <table v-else class="mono">
      <thead>
        <tr><th>干扰机</th><th>频差 MHz</th><th>匹配因子 ζ</th><th>原始功率 mW</th><th>有效功率 mW</th></tr>
      </thead>
      <tbody>
        <tr v-for="d in details" :key="d.jammer_id">
          <td>J{{ d.jammer_id }}</td>
          <td>{{ mhz(d.freq_delta_Hz) }}</td>
          <td>{{ zeta(d.freq_match_factor) }}</td>
          <td>{{ mw(d.original_power_W) }}</td>
          <td>{{ mw(d.effective_power_W) }}</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>

<style scoped>
.detail { padding: 6px 8px; }
table { width: 100%; border-collapse: collapse; font-size: 12px; }
th, td { padding: 4px 8px; text-align: right; border-bottom: 1px solid rgba(255,255,255,.08); }
th:first-child, td:first-child { text-align: left; }
th { color: var(--text-2); font-weight: 500; }
.empty { color: var(--text-2); font-size: 12px; text-align: center; padding: 8px; }
</style>
