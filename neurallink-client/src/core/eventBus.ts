// src/core/eventBus.ts
import mitt from 'mitt';
import { MessageType } from '../protocol/types';
import type { 
  ServerThoughtStream, 
  ServerTtsAudio, 
  ServerAsrResult 
} from '../protocol/types';

// 1. 定义极其严格的事件映射表 (事件名 -> 载荷类型)
// 这样在订阅时，TypeScript 会自动推导出 payload 的结构，拥有完美的智能提示
type Events = {
  // 网络层广播的底层事件
  [MessageType.SERVER_THOUGHT_STREAM]: ServerThoughtStream;
  [MessageType.SERVER_TTS_AUDIO]: ServerTtsAudio;
  [MessageType.SERVER_ASR_RESULT]: ServerAsrResult;
  'system:audio_queue_empty': void;
  // 系统内部事件 (按需添加，比如 UI 触发录音)
  'system:start_recording': void;
  'system:stop_recording': void;
};

// 2. 实例化并导出全局总线
export const bus = mitt<Events>();