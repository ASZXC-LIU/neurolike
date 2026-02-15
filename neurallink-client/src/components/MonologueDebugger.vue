<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import { bus } from '../core/eventBus';
import { MessageType } from '../protocol/types';

// 响应式状态：存储 AI 的思考流
const thoughtStream = ref('');

// 定义事件处理函数
const handleThoughtStream = (payload: { chunk: string; is_end: boolean }) => {
  thoughtStream.value += payload.chunk;
  // 如果当前思考结束，可以做一些换行或格式化处理
  if (payload.is_end) {
    thoughtStream.value += '\n'; 
  }
};

onMounted(() => {
  // 订阅事件：只要网络层收到了思考碎片，这里就会自动更新 UI
  bus.on(MessageType.SERVER_THOUGHT_STREAM, handleThoughtStream);
});

onUnmounted(() => {
  // 组件销毁时注销监听，防止内存泄漏
  bus.off(MessageType.SERVER_THOUGHT_STREAM, handleThoughtStream);
});
</script>

<template>
  <div class="debugger-panel">
    <h3>💡 内心独白 (Inner Monologue)</h3>
    <div class="console-screen">
      <pre>{{ thoughtStream || '等待神经元信号接入...' }}</pre>
    </div>
  </div>
</template>

<style scoped>
.debugger-panel {
  border: 1px solid #30363d;
  border-radius: 8px;
  padding: 15px;
  background: #161b22;
}
h3 { margin-top: 0; color: #58a6ff; }
.console-screen pre {
  white-space: pre-wrap;
  color: #8b949e;
  font-size: 14px;
}
</style>