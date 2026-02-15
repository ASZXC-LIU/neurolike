<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { neuralLink } from './core/NeuralSocket';
import { bus } from './core/eventBus';
import { MessageType } from './protocol/types';
import { setAgentStatus } from './core/stateMachine';
import { initMemoryMatrix } from './core/MemoryManager';
import MonologueDebugger from './components/MonologueDebugger.vue';
import AudioReceiver from './components/AudioReceiver.vue';
import MicRecorder from './components/MicRecorder.vue';

// 🌟 新增：系统就绪状态标志
const systemReady = ref(false);

const bootSystem = () => {
  systemReady.value = true;
  
  // 🌟 核心破局点：播放一段时长 0.1 秒的极小空白音频流
  // 这将以用户的“点击行为”为凭证，向浏览器索要并激活全局 AudioContext 播放权限
  const unlockAudio = new Audio("data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAAABkYXRhAgAAAAEA");
  unlockAudio.play().catch(() => {
    console.warn("静音解锁可能被浏览器拦截，但大概率已成功。");
  });
  initMemoryMatrix();
  // 权限解锁后，安全地连接 WebSocket
  neuralLink.connect();
};

onMounted(() => {
  // ❌ 删除了原本这里的 neuralLink.connect();

  // 监听后端发来音频，意味着 AI 开始说话了
  bus.on(MessageType.SERVER_TTS_AUDIO, () => {
    setAgentStatus('speaking');
  });
});
</script>

<template>
  <div class="neural-dashboard">
    <div v-if="!systemReady" class="boot-screen">
      <h1 class="glitch" data-text="NeuralLink OS">NeuralLink OS</h1>
      <p class="subtitle">System Offline. Waiting for user interaction...</p>
      <button @click="bootSystem" class="boot-btn">
        <span>⚡ 初始化神经链接</span>
      </button>
    </div>

    <template v-else>
      <header>
        <h1>🧠 NeuralLink Control Center</h1>
      </header>
      
      <main class="grid-container">
        <div class="left-panel">
          <MicRecorder />
        </div>
        <div class="right-panel">
          <MonologueDebugger />
        </div>
        <AudioReceiver />
      </main>
    </template>
  </div>
</template>

<style scoped>
.neural-dashboard {
  background-color: #0d1117;
  color: #c9d1d9;
  min-height: 100vh;
  padding: 20px;
  font-family: 'Fira Code', monospace;
}

header { border-bottom: 1px solid #30363d; padding-bottom: 10px; margin-bottom: 20px; }

.grid-container {
  display: grid;
  grid-template-columns: 1fr 2fr;
  gap: 20px;
}

/* 🌟 启动屏样式 */
.boot-screen {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 90vh;
}

.glitch {
  font-size: 3rem;
  font-weight: bold;
  color: #c9d1d9;
  margin-bottom: 10px;
  letter-spacing: 2px;
}

.subtitle {
  color: #8b949e;
  margin-bottom: 40px;
  font-family: monospace;
}

.boot-btn {
  padding: 18px 45px;
  font-size: 20px;
  font-family: 'Fira Code', monospace;
  font-weight: bold;
  background-color: transparent;
  color: #58a6ff;
  border: 2px solid #58a6ff;
  border-radius: 8px;
  cursor: pointer;
  transition: all 0.3s ease;
  box-shadow: 0 0 15px rgba(88, 166, 255, 0.2);
}

.boot-btn:hover {
  background-color: #58a6ff;
  color: #0d1117;
  box-shadow: 0 0 25px rgba(88, 166, 255, 0.6);
  transform: translateY(-2px);
}
</style>