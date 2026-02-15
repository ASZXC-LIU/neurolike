<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue';
import { bus } from '../core/eventBus';
import { MessageType } from '../protocol/types';
import type { ServerTtsAudio } from '../protocol/types';

const audioQueue: string[] = [];
let isPlaying = false;
let currentAudio: HTMLAudioElement | null = null; 

// 🌟 强壮的异步消费循环 (彻底修复死锁)
const playNext = async () => {
  if (isPlaying) return; // 防护：如果已经在播了，别抢麦
  
  if (audioQueue.length === 0) {
    isPlaying = false;
    return; // 队列空了就休息，等待新包唤醒
  }
  
  isPlaying = true;
  const nextAudioB64 = audioQueue.shift();
  
  try {
    currentAudio = new Audio(`data:audio/wav;base64,${nextAudioB64 || ''}`);
    
    // 强制等待当前这句语音播放完毕
    await new Promise((resolve) => {
      if (!currentAudio) return resolve(true);
      
      currentAudio.onended = () => resolve(true);
      currentAudio.onerror = () => resolve(true); // 出错也放行，绝不卡死队列
      
      currentAudio.play().catch(err => {
        console.warn('浏览器静音拦截或播放失败:', err);
        resolve(true);
      });
    });
  } finally {
    currentAudio = null;
    isPlaying = false;
    // 当前语音播完，自动递归拉取队列里的下一句话
    playNext(); 
  }
};

const handleIncomingAudio = (payload: ServerTtsAudio) => {
  // 🌟 收到结束包，挂起释放逻辑
  if (payload.is_reply_end) {
    console.log('🏁 本轮对话文本已全部下发完毕，等待队列排空...');
    // 启动微型轮询：必须等所有积压的语音真正在物理层播完，才切回 idle 状态
    const checkQueue = setInterval(() => {
      if (!isPlaying && audioQueue.length === 0) {
        clearInterval(checkQueue);
        bus.emit('system:audio_queue_empty', undefined);
      }
    }, 100);
    return;
  }

  console.log(`[AudioReceiver] 入队: ${payload.sync_text} | 当前积压长度: ${audioQueue.length}`);
  audioQueue.push(payload.audio_b64);
  
  // 只要有新包进队，就尝试踹一脚播放器
  playNext();
};

const handleInterrupt = () => {
  console.warn('🛑 [AudioReceiver] 收到紧急打断，强行清空队列！');
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