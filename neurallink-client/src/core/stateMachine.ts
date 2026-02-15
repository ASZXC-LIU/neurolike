// src/core/stateMachine.ts
import { reactive } from 'vue';

export type AgentStatus = 'idle' | 'listening' | 'processing' | 'speaking';

export const agentContext = reactive({
  status: 'idle' as AgentStatus,
  moodScore: 50,          // 情绪值 (用于后续联动 Live2D)
  disturbTolerance: 3     // 干扰容忍度
});

// 状态切换守卫
export const setAgentStatus = (newStatus: AgentStatus) => {
  console.log(`[状态机] 切换状态: ${agentContext.status} ➡️ ${newStatus}`);
  agentContext.status = newStatus;
};