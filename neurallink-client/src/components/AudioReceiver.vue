<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue';
import { bus } from '../core/eventBus';
import { MessageType } from '../protocol/types';
import type { ServerTtsAudio } from '../protocol/types';

const audioQueue: string[] = [];
let isPlaying = false;
let currentAudio: HTMLAudioElement | null = null; 

// 唯一的 playNext 函数
const playNext = async () => {
  if (audioQueue.length === 0) {
    isPlaying = false;
    bus.emit('system:audio_queue_empty', undefined);
    return;
  }
  
  isPlaying = true;
  const nextAudioB64 = audioQueue.shift();
  // 添加 || '' 防止 base64 为 undefined 导致报错
  currentAudio = new Audio(`data:audio/wav;base64,${nextAudioB64 || ''}`);
  
  currentAudio.onended = () => {
    currentAudio = null;
    playNext();
  };
  
  await currentAudio.play();
};

const handleIncomingAudio = (payload: ServerTtsAudio) => {
  // 🌟 收到结束包，直接退出，不放入音频缓冲池
  if (payload.is_reply_end) {
    console.log('🏁 本轮对话文本已全部下发完毕');
    return;
  }
  console.log(`[AudioReceiver] 收到语音包，对应文本: ${payload.sync_text}`);
  audioQueue.push(payload.audio_b64);
  
  if (!isPlaying) {
    playNext();
  }
};

const handleInterrupt = () => {
  console.warn('🛑 [AudioReceiver] 收到紧急打断信号，清空播放队列！');
  audioQueue.length = 0; 
  if (currentAudio) {
    currentAudio.pause(); 
    currentAudio = null;
  }
  isPlaying = false;
};

onMounted(() => {
  bus.on(MessageType.SERVER_TTS_AUDIO, handleIncomingAudio);
  bus.on(MessageType.CLIENT_INTERRUPT as any, handleInterrupt);
});

onUnmounted(() => {
  bus.off(MessageType.SERVER_TTS_AUDIO, handleIncomingAudio);
  bus.off(MessageType.CLIENT_INTERRUPT as any, handleInterrupt);
});
</script>

<template>
  <div style="display: none;"></div>
</template>