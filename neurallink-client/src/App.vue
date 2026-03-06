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
// 🌟 Task 4.8: 主控权状态
const isMaster = ref(false);

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

// 🌟 Task 4.8: 抢占主控权
const requestMaster = () => {
  if (!neuralLink.clientId) return;
  neuralLink.send(MessageType.CLIENT_REQUEST_MASTER, {
    client_id: neuralLink.clientId,
    reason: "manual_override"
  });
};

onMounted(() => {
  // ❌ 删除了原本这里的 neuralLink.connect();

  // 监听后端发来音频，意味着 AI 开始说话了
  bus.on(MessageType.SERVER_TTS_AUDIO, () => {
    setAgentStatus('speaking');
  });

  // 🌟 Task 4.8: 监听主控权变更
  bus.on(MessageType.SERVER_MASTER_GRANT, (payload: any) => {
    // 如果 Server 广播的 master_client_id 等于我的 ID，那我就是 Master
    if (payload.master_client_id === neuralLink.clientId) {
      isMaster.value = true;
      console.log("👑 我已成为 Master 节点");
    } else {
      isMaster.value = false;
      console.log(`🛡️ Master 权已移交至: ${payload.master_client_id}`);
    }
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
        <!-- 🌟 Task 4.8: 抢占按钮 -->
        <div class="master-control">
           <button 
            @click="requestMaster" 
            :class="['master-btn', { active: isMaster }]"
          >
            {{ isMaster ? '👑 主控模式 (Master)' : '🛡️ 抢占控制权 (Slave)' }}
          </button>
        </div>
      </header>
      
      <main class="grid-container">
        <div class="left-panel">
          <!-- 🌟 Task 4.8: 只有 Master 才能录音 -->
          <MicRecorder v-if="isMaster" />
          <div v-else class="slave-mode-tip">
            <p>当前为从机模式，仅接收消息。</p>
            <p>请点击右上角抢占控制权以启用麦克风。</p>
          </div>
        </div>
        <div class="right-panel">
          <MonologueDebugger />
        </div>
        <!-- 🌟 Task 4.8: AudioReceiver 内部需要判断 isMaster -->
        <AudioReceiver :is-master="isMaster" />
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

header { 
  border-bottom: 1px solid #30363d; 
  padding-bottom: 10px; 
  margin-bottom: 20px; 
  display: flex;
  justify-content: space-between;
  align-items: center;
}

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

/* 🌟 Task 4.8: Master 按钮样式 */
.master-btn {
  padding: 8px 16px;
  border-radius: 6px;
  font-family: 'Fira Code', monospace;
  cursor: pointer;
  transition: all 0.2s;
  border: 1px solid #30363d;
  background-color: #21262d;
  color: #8b949e;
}

.master-btn.active {
  background-color: #238636;
  color: #ffffff;
  border-color: #238636;
  box-shadow: 0 0 10px rgba(35, 134, 54, 0.4);
}

.master-btn:hover:not(.active) {
  background-color: #30363d;
  color: #c9d1d9;
}

.slave-mode-tip {
  padding: 20px;
  border: 1px dashed #30363d;
  border-radius: 8px;
  color: #8b949e;
  text-align: center;
  margin-top: 20px;
}
</style>
