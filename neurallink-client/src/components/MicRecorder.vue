<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue';
import { neuralLink } from '../core/NeuralSocket';
import { MessageType } from '../protocol/types';
import type { ServerEmotionShift } from '../protocol/types';
import { agentContext, setAgentStatus } from '../core/stateMachine';
import { bus } from '../core/eventBus';
// 引入 hark 库 (需确保已安装: npm install hark @types/hark)
// 如果没有 hark，我们暂时用模拟逻辑或原生 AudioContext 实现
// 这里假设我们使用原生 AudioContext 实现简单的 VAD 阈值控制

const isRecording = ref(false);
let mediaRecorder: MediaRecorder | null = null;
let audioStream: MediaStream | null = null;
let audioContext: AudioContext | null = null;
let analyser: AnalyserNode | null = null;
let microphone: MediaStreamAudioSourceNode | null = null;

// 🌟 Task 1.6: 动态 VAD 阈值
// 默认 -50dB (标准抗噪)
const currentVadThreshold = ref(-50); 

// 🌟 核心修复 1：创建一个全局的 Promise 队列，强制保证异步切片按绝对顺序发送
let sendQueue = Promise.resolve();

const blobToBase64 = (blob: Blob): Promise<string> => {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onloadend = () => {
      const dataUrl = reader.result as string;
      const base64 = dataUrl.split(',')[1] || ''; 
      resolve(base64);
    };
    reader.onerror = reject;
    reader.readAsDataURL(blob);
  });
};

const initAudio = async () => {
  try {
    audioStream = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: false, autoGainControl: false }
    });
    
    // 初始化 AudioContext 用于 VAD 分析
    audioContext = new AudioContext();
    analyser = audioContext.createAnalyser();
    analyser.fftSize = 256;
    microphone = audioContext.createMediaStreamSource(audioStream);
    microphone.connect(analyser);
    
    console.log('✅ [MicRecorder] 麦克风硬件权限获取成功');
  } catch (err) {
    console.error('❌ [MicRecorder] 无法访问麦克风:', err);
  }
};

// 🌟 Task 1.6: 监听服务端情绪变化，动态调整 VAD 阈值
const handleEmotionShift = (payload: ServerEmotionShift) => {
  if (payload.vad_sensitivity !== undefined) {
    // 映射逻辑: sensitivity (0.0-1.0) -> threshold (-60dB to -40dB)
    // 1.0 (最灵敏) -> -60dB
    // 0.0 (最迟钝) -> -40dB
    const newThreshold = -40 - (payload.vad_sensitivity * 20);
    currentVadThreshold.value = Math.round(newThreshold);
    console.log(`🎚️ [MicRecorder] VAD 阈值已根据情绪调整为: ${currentVadThreshold.value}dB (灵敏度: ${payload.vad_sensitivity})`);
  }
};

const startRecording = () => {
  if (!audioStream) return;
  if (agentContext.status === 'speaking') {
    bus.emit(MessageType.CLIENT_INTERRUPT as any, { reason: 'barge_in' });
    // 2. 🌟 核心补齐：通过 WebSocket 跨端狙击后端的正在执行的任务！
    neuralLink.send(MessageType.CLIENT_INTERRUPT, { reason: 'barge_in' });
  }

  isRecording.value = true;
  setAgentStatus('listening');

  mediaRecorder = new MediaRecorder(audioStream, { mimeType: 'audio/webm' });

  // 🌟 核心修复 2：将发送任务排入传送带，防止 Chunk 2 抢跑超过 Chunk 1
  mediaRecorder.ondataavailable = (event) => {
    if (event.data.size > 0) {
      sendQueue = sendQueue.then(async () => {
        const b64 = await blobToBase64(event.data);
        neuralLink.send(MessageType.CLIENT_AUDIO_CHUNK, {
          audio_b64: b64,
          is_last: false 
        });
      });
    }
  };

  // 🌟 核心修复 3：结束标志也必须在传送带末尾排队
  mediaRecorder.onstop = () => {
    sendQueue = sendQueue.then(() => {
      neuralLink.send(MessageType.CLIENT_AUDIO_CHUNK, {
        audio_b64: "",
        is_last: true 
      });
    });
  };

  mediaRecorder.start();
};

const stopRecording = () => {
  if (!mediaRecorder || mediaRecorder.state === 'inactive') return;
  isRecording.value = false;
  setAgentStatus('processing');
  mediaRecorder.stop();
};

onMounted(async () => {
  await initAudio();
  bus.on('system:audio_queue_empty', () => {
    if (agentContext.status === 'speaking') {
      setAgentStatus('idle');
    }
  });
  // 注册情绪监听
  bus.on(MessageType.SERVER_EMOTION_SHIFT, handleEmotionShift);
});

onUnmounted(() => {
  if (audioStream) audioStream.getTracks().forEach(track => track.stop());
  if (audioContext) audioContext.close();
  bus.off(MessageType.SERVER_EMOTION_SHIFT, handleEmotionShift);
});
</script>

<template>
  <div class="mic-panel">
    <div class="status-badge" :class="agentContext.status">
      当前状态: {{ agentContext.status.toUpperCase() }}
    </div>
    <div class="vad-indicator">
      当前听觉灵敏度: {{ currentVadThreshold }}dB
    </div>
    <button class="record-btn" :class="{ recording: isRecording }"
      @mousedown="startRecording" @mouseup="stopRecording" @mouseleave="stopRecording">
      {{ isRecording ? '🎙️ 正在倾听 (松开发送)...' : '⏸️ 按住说话' }}
    </button>
  </div>
</template>

<style scoped>
/* 样式保持不变 */
.mic-panel { display: flex; flex-direction: column; align-items: center; gap: 15px; padding: 20px; background: #161b22; border: 1px solid #30363d; border-radius: 8px; }
.status-badge { padding: 5px 15px; border-radius: 20px; font-weight: bold; font-size: 14px; }
.vad-indicator { font-size: 12px; color: #8b949e; }
.idle { background: #484f58; color: white; }
.listening { background: #238636; color: white; }
.processing { background: #d29922; color: black; }
.speaking { background: #8957e5; color: white; }
.record-btn { padding: 15px 30px; font-size: 18px; border-radius: 8px; border: none; background-color: #21262d; color: #c9d1d9; cursor: pointer; transition: all 0.2s; user-select: none; }
.record-btn:active, .record-btn.recording { background-color: #da3633; transform: scale(0.95); box-shadow: 0 0 15px rgba(218, 54, 51, 0.5); }
</style>
