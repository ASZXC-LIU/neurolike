// src/core/NeuralSocket.ts
import { bus } from './eventBus';
import { MessageType, type WsMessage } from '../protocol/types';

export class NeuralSocket {
  private ws: WebSocket | null = null;
  private url: string;
  private reconnectTimer: number | null = null;
  // 🌟 Task 4.8: 存储 Client ID
  public clientId: string = "";

  constructor(url: string = 'ws://127.0.0.1:8000/ws') {
    this.url = url;
    // 🌟 Task 4.8: 生成 Client ID
    this.clientId = crypto.randomUUID();
  }

  public connect() {
    console.log('[NeuralSocket] 正在连接 3060 算力节点...');
    this.ws = new WebSocket(this.url);

    this.ws.onopen = () => {
      console.log('✅ [NeuralSocket] 神经元直连通道已建立！');
      if (this.reconnectTimer) clearInterval(this.reconnectTimer);
      
      // 发送握手鉴权 (根据你的文档)
      this.send(MessageType.CLIENT_AUTH_HANDSHAKE, {
        access_token: "neural_link_secret_2026",
        client_version: "1.3"
      });
    };

    this.ws.onmessage = (event) => {
      try {
        const envelope: WsMessage = JSON.parse(event.data);
        
        // 🔥 核心解耦逻辑：网络层不做任何业务处理，直接把 payload 广播到总线上
        // 任何关心这个事件的 Vue 组件（如调试面板、Live2D）自己去订阅
        bus.emit(envelope.type as any, envelope.payload);
        
      } catch (err) {
        console.error('❌ 数据包解析碎裂:', err);
      }
    };

    this.ws.onclose = () => {
      console.warn('⚠️ [NeuralSocket] 连接断开，触发自愈重连机制...');
      this.scheduleReconnect();
    };

    this.ws.onerror = (err) => {
      console.error('❌ [NeuralSocket] 通讯异常:', err);
    };
  }

  public send(type: MessageType, payload: any) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      const msg: WsMessage = {
        msg_id: crypto.randomUUID(),
        type,
        timestamp: Date.now(),
        payload
      };
      this.ws.send(JSON.stringify(msg));
    } else {
      console.warn('⚠️ 尝试发送数据失败：网络尚未就绪');
    }
  }

  private scheduleReconnect() {
    if (!this.reconnectTimer) {
      this.reconnectTimer = window.setInterval(() => {
        console.log('🔄 正在尝试重新建立神经连接...');
        this.connect();
      }, 3000);
    }
  }
}

// 导出一个单例供全局使用
export const neuralLink = new NeuralSocket();
