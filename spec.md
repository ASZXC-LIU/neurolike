# Feature Specification: Task 1.6 基于情绪的动态 VAD 阈值调整

## 1. 目标 (Goal)
当 AI 处于“极度关注”或“生气”等高唤醒度状态时，自动降低前端麦克风的 VAD (Voice Activity Detection) 能量阈值，使其能捕捉到用户的细微声音（如低语、叹气），模拟人类在专注时的听觉敏感度提升。

## 2. 核心逻辑 (Core Logic)

### 2.1 情绪状态下发
- **Server**: LLM 在 `thought` 中推演出当前 AI 的情绪状态（如 `Mood_Score` 或 `Emotion_Tag`）。
- **Protocol**: 服务端通过 `SERVER_EMOTION_SHIFT` 消息将情绪状态下发给客户端。

### 2.2 动态阈值映射
- **Client**: 接收到 `SERVER_EMOTION_SHIFT` 后，根据情绪类型调整 VAD 阈值。
  - **默认状态 (Neutral)**: 阈值 `-50dB` (标准抗噪)。
  - **高唤醒 (Angry/Focus)**: 阈值 `-60dB` (更灵敏，易触发)。
  - **低唤醒 (Sleepy/Sad)**: 阈值 `-45dB` (迟钝，不易打扰)。

### 2.3 VAD 参数热更新
- **AudioContext**: 前端音频处理模块需支持在不重启录音的情况下，动态修改 `AudioWorklet` 或 `ScriptProcessor` 的参数。

## 3. 协议定义 (Protocol)

### 3.1 新增消息类型
无需新增，复用 `SERVER_EMOTION_SHIFT`。

```typescript
// neurallink-client/src/protocol/types.ts
export interface ServerEmotionShift {
  ai_mood_score: number;       
  live2d_expression: string; 
  live2d_motion: string;     
  vad_sensitivity?: number; // [新增] 0.0 - 1.0 (1.0 最灵敏)
}
```

## 4. 实现步骤 (Implementation Steps)

1.  **Server (`main.py`)**:
    - 在 `run_llm_inference` 中，解析 LLM 的情绪输出。
    - 根据情绪计算 `vad_sensitivity`，并随 `SERVER_EMOTION_SHIFT` 下发。
    - *注: 暂时简单映射：Angry/Happy -> 0.9, Neutral -> 0.5, Sad -> 0.3*

2.  **Client (`types.ts`)**:
    - 更新 `ServerEmotionShift` 接口定义。

3.  **Client (`AudioRecorder.ts` - 需新建或修改)**:
    - 监听 `SERVER_EMOTION_SHIFT` 事件。
    - 实现 `setVadThreshold(sensitivity)` 方法，动态调整 `Hark` 或 `AudioContext` 的阈值。

## 5. 验证标准 (Verification Criteria)
- [ ] 模拟 AI 生气 (Angry)，前端 VAD 阈值应自动降低（日志显示 "VAD Sensitivity adjusted to 0.9"）。
- [ ] 此时用户轻声说话应能被识别。
- [ ] 模拟 AI 犯困 (Sleepy)，VAD 阈值升高，忽略背景噪音。
