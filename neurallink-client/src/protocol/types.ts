/**
 * ==========================================
 * NeuralLink Private Agent - WebSocket 核心协议
 * 版本: V1.3 (Ultimate Edition)
 * 架构: 双机全双工 (主力机 Node.js <-> 3060 FastAPI)
 * ==========================================
 */

// ------------------------------------------
// 1. 基础信封协议 & 路由枚举
// ------------------------------------------
export type MessageType = typeof MessageType[keyof typeof MessageType];
export interface WsMessage<T = any> {
  msg_id: string;          // 消息UUID
  type: MessageType;       // 路由类型
  timestamp: number;       // 毫秒级时间戳
  payload: T;              // 业务包
}

export const MessageType = {
  CLIENT_AUTH_HANDSHAKE: "client.auth_handshake",
  CLIENT_WAKE_UP: "client.wake_up",
  CLIENT_AUDIO_CHUNK: "client.audio_chunk",
  CLIENT_TEXT_REQUEST: "client.text_request",
  CLIENT_INTERRUPT: "client.interrupt",
  CLIENT_SYSTEM_TICK: "client.system_tick",
  CLIENT_EMBED_REQUEST: "client.embed_request",
  CLIENT_ACTION_RESULT: "client.action_result",

  SERVER_ASR_RESULT: "server.asr_result",
  SERVER_THOUGHT_STREAM: "server.thought_stream",
  SERVER_EMOTION_SHIFT: "server.emotion_shift",
  SERVER_INTENT_CALL: "server.intent_call",
  SERVER_TTS_AUDIO: "server.tts_audio",
  SERVER_EMBED_RESULT: "server.embed_result",
  SERVER_ERROR: "server.error"
} as const;

// ------------------------------------------
// 2. 上行数据包定义 (Client -> Server)
// ------------------------------------------

export interface ClientAuthHandshake {
  access_token: string;      // 预共享密钥，防局域网盗用
  client_version: string;
}

export interface ClientWakeUp {
  trigger_reason: "mouse_move" | "mic_unmuted" | "app_foreground" | "manual";
}

export interface ClientAudioChunk {
  audio_b64: string;         // Base64 PCM/WAV 
  is_last: boolean;          // VAD 截断标志
}

export interface ClientTextRequest {
  text: string;
  ai_current_state: {        // [V1.3 核心补丁] AI 当前心理状态，保持性格连贯
    mood_score: number;      
    attitude: string;        
  };
  chat_history: Array<{ role: "user" | "assistant"; content: string }>;
  rag_context?: string;      // 检索出的记忆事实
  visual_context?: { active_window: string; focus_text: string }; 
  disturb_tolerance: number; 
}

export interface ClientInterrupt {
  reason: "barge_in" | "physical_kill"; 
}

export interface ClientSystemTick {
  current_time: string;      
  user_idle_time: number;    
  active_window: string;     
  battery_level?: number;    
  disturb_tolerance: number; 
}

export interface ClientEmbedRequest {
  memory_id: string;         // [微观补齐] 追踪并发向量任务
  memory_text: string;       
}

export interface ClientActionResult {
  msg_id: string;            
  action: string;            
  status: "success" | "failed"; 
  detail: string;            
}

// ------------------------------------------
// 3. 下行数据包定义 (Server -> Client)
// ------------------------------------------

export interface ServerAsrResult {
  text: string;              
  emotion: string;           
  confidence: number;        
  is_valid_speech: boolean;  // 拦截纯噪音
}

export interface ServerThoughtStream {
  chunk: string;             
  is_end: boolean;           
}

export interface ServerEmotionShift {
  ai_mood_score: number;       
  live2d_expression: string; // [微观补齐] 脸部表情 (如: blush, angry)
  live2d_motion: string;     // [微观补齐] 身体动作 (如: nod, shake_head)
  reason?: string;           
  vad_sensitivity?: number;  // [Task 1.6 新增] 0.0 - 1.0 (1.0 最灵敏)
}

export interface ServerIntentCall {
  action: string;              
  target?: string;             
  parameters: Record<string, any>; 
}

export interface ServerTtsAudio {
  audio_b64: string;         
  volume_db: number;         // 驱动 ParamMouthOpenY
  sample_rate: number;       
  sentence_id: number;       
  sync_text: string;         // 解决音画撕裂的强绑定字幕
  is_reply_end: boolean;     // [微观补齐] 标志这轮对话的音频是否已全部下发完毕，用于释放状态机
}

export interface ServerEmbedResult {
  memory_id: string;         // [微观补齐] 原路返回，供 Client 对号入座
  vector: number[];          
}

export interface ServerError {
  err_code: string;          
  err_msg: string;           
  suggested_action: "reset_to_idle" | "fallback_to_offline"; 
}