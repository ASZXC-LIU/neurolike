import { reactive } from 'vue';
import { bus } from './eventBus';
import { neuralLink } from './NeuralSocket';
import { MessageType } from '../protocol/types';

// 记忆池状态
export const memoryState = reactive({
  chatHistory: [] as Array<{ role: 'user' | 'assistant'; content: string }>
});

const MEMORY_KEY = 'neural_link_memory_v1';
const MAX_CONTEXT_ROUNDS = 5; // Task 2.2: 滑动窗口，每次只带最近 5 轮对话

// 初始化加载本地数据库 (伪装的 SQLite 落盘)
export const initMemoryMatrix = () => {
  const saved = localStorage.getItem(MEMORY_KEY);
  if (saved) {
    memoryState.chatHistory = JSON.parse(saved);
    console.log(`🧠 [Memory] 成功从本地加载了 ${memoryState.chatHistory.length} 条记忆碎片。`);
  }

  // 1. 监听用户的声音被识别出来
  bus.on(MessageType.SERVER_ASR_RESULT, (payload) => {
    if (!payload.is_valid_speech || !payload.text) return;

    // 存入短期记忆
    memoryState.chatHistory.push({ role: 'user', content: payload.text });
    localStorage.setItem(MEMORY_KEY, JSON.stringify(memoryState.chatHistory));

    // 🌟 核心突破：由前端发起主动权，携带上下文请求 3060
    console.log('📤 [Memory] 正在打包上下文，请求皮层响应...');
    neuralLink.send(MessageType.CLIENT_TEXT_REQUEST, {
      text: payload.text,
      ai_current_state: { mood_score: 50, attitude: "neutral" },
      chat_history: memoryState.chatHistory.slice(-MAX_CONTEXT_ROUNDS * 2), // 取最近 N 轮
      disturb_tolerance: 3
    });
  });

  // 2. 监听 AI 回复并收集
  let currentAiReply = "";
  bus.on(MessageType.SERVER_TTS_AUDIO, (payload) => {
    // 🌟 过滤掉 sentence_id=0 的填充音 (如 "嗯...")，不计入长期记忆
    if (payload.sentence_id === 0) {
      console.log('🙊 [Memory] 忽略填充音，不写入历史');
      return;
    }

    if (payload.sync_text) {
      currentAiReply += payload.sync_text;
    }
    
    // AI 这句话说完了，或者被中途打断了，把积攒的句子存入记忆库
    if (payload.is_reply_end) {
      if (currentAiReply.trim()) {
        memoryState.chatHistory.push({ role: 'assistant', content: currentAiReply });
        localStorage.setItem(MEMORY_KEY, JSON.stringify(memoryState.chatHistory));
        currentAiReply = ""; // 重置，准备下一轮
      }
    }
  });

  // 3. 拦截打断事件：如果被打断，也要把说了一半的话记下来
  bus.on(MessageType.CLIENT_INTERRUPT as any, () => {
    if (currentAiReply.trim()) {
      memoryState.chatHistory.push({ role: 'assistant', content: currentAiReply + "-(被打断)" });
      localStorage.setItem(MEMORY_KEY, JSON.stringify(memoryState.chatHistory));
      currentAiReply = ""; 
    }
  });
};