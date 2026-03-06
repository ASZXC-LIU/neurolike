import json
import logging
import base64
import re
import time
import os
from contextlib import suppress
from enum import Enum
from typing import Dict, Any, Optional
import httpx
import io
import uuid
from pydub import AudioSegment
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from pydantic import BaseModel
from funasr import AutoModel
import google.generativeai as genai
import asyncio

# ==========================================
# 0. 初始化与模型加载
# ==========================================
logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(levelname)s - %(message)s")
logger = logging.getLogger("NeuralLink_Brain")

logger.info("🧠 正在加载 SenseVoiceSmall...")
asr_model = AutoModel(model="iic/SenseVoiceSmall", trust_remote_code=True, device="cuda:0")


def process_and_recognize(audio_bytes: bytes) -> list:
    """负责将非标准 WebM/PCM 字节流洗成 16kHz WAV，并直接喂给大模型"""
    audio_io = io.BytesIO(audio_bytes)
    audio_segment = AudioSegment.from_file(audio_io)

    # 强制重采样为 ASR 黄金标准
    audio_segment = audio_segment.set_frame_rate(16000).set_channels(1).set_sample_width(2)

    wav_io = io.BytesIO()
    audio_segment.export(wav_io, format="wav")
    wav_bytes = wav_io.getvalue()

    # 直接调用全局的 asr_model
    return asr_model.generate(input=wav_bytes, cache={}, language="zh", use_itn=True)
genai.configure(api_key="AIzaSyA8n3UNUSg-4Ln9WWY_jtvUxmxRH4FMwjQ")


def _read_int_env(key: str, default: int) -> int:
    value = os.getenv(key)
    if value is None:
        return default
    try:
        return int(value)
    except ValueError:
        logger.warning("环境变量 %s=%s 非法，回退默认值 %s", key, value, default)
        return default


def _read_float_env(key: str, default: float) -> float:
    value = os.getenv(key)
    if value is None:
        return default
    try:
        return float(value)
    except ValueError:
        logger.warning("环境变量 %s=%s 非法，回退默认值 %s", key, value, default)
        return default


LATENCY_MASK_THRESHOLD_MS = _read_int_env("LATENCY_MASK_THRESHOLD_MS", 600)
LATENCY_MASK_FILLER_TEXT = os.getenv("LATENCY_MASK_FILLER_TEXT", "嗯……")
LATENCY_MASK_TTS_TIMEOUT_SEC = _read_float_env("LATENCY_MASK_TTS_TIMEOUT_SEC", 3.0)


# ==========================================
# 1. 协议枚举 (修复了与前端不匹配的枚举)
# ==========================================
class MessageType(str, Enum):
    CLIENT_AUTH_HANDSHAKE = "client.auth_handshake"
    CLIENT_WAKE_UP = "client.wake_up"
    CLIENT_AUDIO_CHUNK = "client.audio_chunk"
    CLIENT_TEXT_REQUEST = "client.text_request"  # 🌟 新增：携带记忆的文本请求
    CLIENT_INTERRUPT = "client.interrupt"  # 🌟 新增：确保打断指令在枚举中
    CLIENT_REQUEST_MASTER = "client.request_master" # 🌟 Task 4.8 新增：申请主控权

    SERVER_ASR_RESULT = "server.asr_result"
    SERVER_THOUGHT_STREAM = "server.thought_stream"
    SERVER_TTS_AUDIO = "server.tts_audio"  # 🌟 修复点：与前端保持绝对一致
    SERVER_EMOTION_SHIFT = "server.emotion_shift" # 🌟 Task 1.6 新增：情绪状态下发
    SERVER_MASTER_GRANT = "server.master_grant" # 🌟 Task 4.8 新增：广播主控权变更
    SERVER_ERROR = "server.error"


class WsMessage(BaseModel):
    msg_id: str
    client_type: Optional[str] = None # 🌟 Task 4.8 新增：客户端类型
    type: MessageType
    timestamp: int
    payload: Dict[str, Any]


class ClientAudioChunk(BaseModel):
    audio_b64: str
    is_last: bool


# ==========================================
# 2. 核心逻辑处理引擎
# ==========================================
class NeuralLinkEngine:
    # 🌟 新增：类级别的全局缓存，存放填充音的 Base64，全局复用
    _filler_audio_b64: str = None
    # 🌟 Task 4.8 新增：全局 Master ID
    _current_master_id: str = None
    # 🌟 Task 4.8 新增：全局连接池，用于广播
    _connected_clients: list = []

    @classmethod
    async def warmup_filler_audio(cls) -> bool:
        if cls._filler_audio_b64:
            return True

        try:
            async with httpx.AsyncClient() as client:
                res = await client.get(
                    "http://127.0.0.1:9880/tts",
                    params={"text": LATENCY_MASK_FILLER_TEXT},
                    timeout=LATENCY_MASK_TTS_TIMEOUT_SEC,
                )
            if res.status_code == 200:
                cls._filler_audio_b64 = base64.b64encode(res.content).decode("utf-8")
                logger.info("✅ 延迟掩盖填充音预热完成: %s", LATENCY_MASK_FILLER_TEXT)
                return True
            logger.warning("填充音预热失败: HTTP %s", res.status_code)
        except Exception as e:
            logger.warning("填充音预热失败（服务继续运行）: %s", e)
        return False

    def __init__(self, websocket: WebSocket):
        self.ws = websocket
        self.is_authenticated = False
        self.audio_buffer = []  # 音频拼接缓存区
        self.client_id = str(uuid.uuid4()) # 为每个连接分配唯一 ID

        self.current_task_id = None
        
        # 注册到连接池
        NeuralLinkEngine._connected_clients.append(self)

    async def send_message(self, msg_id: str, msg_type: MessageType, payload: dict):
        msg = {
            "msg_id": msg_id,
            "type": msg_type,
            "timestamp": int(time.time() * 1000),
            "payload": payload
        }
        try:
            await self.ws.send_text(json.dumps(msg))
        except Exception as e:
            logger.warning(f"发送消息失败: {e}")

    # 🌟 Task 4.8 新增：广播消息给所有客户端
    @classmethod
    async def broadcast_message(cls, msg_id: str, msg_type: MessageType, payload: dict):
        for client in cls._connected_clients:
            if client.ws.client_state.name == "CONNECTED":
                await client.send_message(msg_id, msg_type, payload)

    async def run_llm_inference(self, msg_id: str, user_text: str, chat_history: list, task_id: str):
        logger.info(f"✨ 正在通过 Gemini 思考: {user_text}")

        # 🌟 动态构建上下文记忆字符串
        history_prompt = ""
        if chat_history:
            history_prompt = "【历史对话记录】\n"
            for msg in chat_history:
                role = "主人" if msg.get("role") == "user" else "小智"
                history_prompt += f"{role}说：{msg.get('content')}\n"
            history_prompt += "【历史结束】\n\n"

        prompt = (
            f"你是一个名为小智的 AI 助手。请不要在回复中使用 Emoji 表情，确保文本纯净以便语音合成。\n"
            f"格式如下：{{\"thought\": \"内心独白\", \"speak\": \"你要说的话\"}}。\n"
            f"注意：你的 thought 思考过程严格控制在 30 字以内，精简扼要！\n"
            f"🌟 情感表达：你可以使用 [speed=1.2] (加快) 或 [speed=0.8] (减慢) 来控制语速，例如：'[speed=1.5]太棒了！'\n"
            f"🌟 情绪状态：请在 thought 中明确你的情绪状态 (Mood)，例如：'Mood: Happy', 'Mood: Angry', 'Mood: Sad', 'Mood: Neutral'。\n"
            f"用户说：{user_text}"
        )
        try:
            model = genai.GenerativeModel("gemini-3-pro-preview")
            # 🌟 核心突破 1：真实开启流式接收
            response_stream = await model.generate_content_async(prompt, stream=True)

            # 流式状态游标
            full_text = ""
            emitted_thought_len = 0
            emitted_speak_len = 0
            sentence_buffer = ""
            sentence_id = 0

            #  核心突破 2：TTS 异步传送带 (Queue)
            tts_queue = asyncio.Queue()
            #  Task 1.4 新增：是否已经下发了第一句正式语音的标志
            task_state = {"first_audio_sent": False, "latency_mask_sent": False}

            #  Task 1.4 新增：延迟掩盖看门狗协程
            async def latency_mask_worker():
                # 倒计时阈值 (默认 600ms，可配置)
                await asyncio.sleep(max(LATENCY_MASK_THRESHOLD_MS, 0) / 1000.0)

                # 如果超时后，第一句正式回复还没生成，且用户没有打断
                if (
                    not task_state["first_audio_sent"]
                    and not task_state["latency_mask_sent"]
                    and self.current_task_id == task_id
                ):
                    logger.info("⏳ 思考时间超过 %sms，触发延迟掩盖机制...", LATENCY_MASK_THRESHOLD_MS)

                    # 优先命中启动期缓存；缺失时再按需拉取一次
                    if not NeuralLinkEngine._filler_audio_b64:
                        ready = await NeuralLinkEngine.warmup_filler_audio()
                        if not ready:
                            logger.warning("填充音获取失败，跳过掩盖")
                            return

                    # 再次校验状态，防止在请求 TTS 期间发生改变
                    if (
                        not task_state["first_audio_sent"]
                        and not task_state["latency_mask_sent"]
                        and self.current_task_id == task_id
                        and NeuralLinkEngine._filler_audio_b64
                    ):
                        task_state["latency_mask_sent"] = True
                        logger.info("👄 下发填充音: %s", LATENCY_MASK_FILLER_TEXT)
                        # 🌟 Task 4.8: 广播 TTS 音频，让所有客户端都能收到（但只有 Master 播放）
                        await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_TTS_AUDIO, {
                            "audio_b64": NeuralLinkEngine._filler_audio_b64,
                            "sync_text": LATENCY_MASK_FILLER_TEXT,  # 前端可以静默显示，或作为特效
                            "sentence_id": 0,  # 0 代表这是一个辅助音
                            "is_reply_end": False,
                            "is_filler_audio": True
                        })

            # 启动看门狗任务
            mask_task = asyncio.create_task(latency_mask_worker())

            # --- 定义后台消费者：异步拉取句子，请求 TTS，推给前端 ---
            async def tts_worker():
                while True:
                    if self.current_task_id != task_id:
                        break  # 任务被打断，消费者直接下班

                    item = await tts_queue.get()
                    if item is None:  # 收到毒药（结束信号），安全退出
                        tts_queue.task_done()
                        break

                    s_id, text_chunk = item
                    logger.info(f"🎙️ 正在向 3060 节点请求语音合成 [{s_id}]: {text_chunk}")
                    
                    # 🌟 Task 1.5: SSML 标签解析器
                    speed_factor = 1.0
                    # 提取 [speed=1.2]
                    speed_match = re.search(r'\[speed=([\d\.]+)\]', text_chunk)
                    if speed_match:
                        try:
                            speed_factor = float(speed_match.group(1))
                            # 剔除标签，只保留纯文本
                            text_chunk = re.sub(r'\[speed=[\d\.]+\]', '', text_chunk)
                            logger.info(f"🚀 动态语速调整: {speed_factor}x")
                        except ValueError:
                            pass

                    try:
                        async with httpx.AsyncClient() as client:
                            tts_url = "http://127.0.0.1:9880/tts"
                            tts_res = await client.get(tts_url, params={
                                "text": text_chunk,
                                "speed_factor": speed_factor
                            }, timeout=15.0)

                        if self.current_task_id != task_id:
                            tts_queue.task_done()
                            break  # TTS 合成回来后，再次检查是否被打断

                        if tts_res.status_code == 200:
                            #  Task 1.4 新增：真正的正文语音回来了，立刻关门打狗！
                            if not task_state["first_audio_sent"]:
                                task_state["first_audio_sent"] = True
                                mask_task.cancel()  # 取消看门狗倒计时（如果还没触发的话）

                            audio_b64 = base64.b64encode(tts_res.content).decode('utf-8')
                            # 🌟 Task 4.8: 广播 TTS 音频
                            await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_TTS_AUDIO, {
                                "audio_b64": audio_b64,
                                "sync_text": text_chunk,
                                "sentence_id": s_id,
                                "is_reply_end": False,
                                "is_filler_audio": False
                            })
                    except Exception as e:
                        logger.error(f"❌ TTS 服务故障: {e}")
                    finally:
                        tts_queue.task_done()

            # 启动 TTS 消费者并发协程
            tts_task = asyncio.create_task(tts_worker())

            #  核心突破 3：生产者 (正则暴力撕开未闭合的 JSON)
            async for chunk in response_stream:
                if self.current_task_id != task_id:
                    logger.warning("🛑 任务已作废，掐断大脑思考流！")
                    break

                try:
                    chunk_text = chunk.text
                except Exception:
                    continue  # 规避 Gemini 安全拦截导致的 chunk 无文本报错

                if chunk_text:
                    full_text += chunk_text

                    # 匹配 "thought": " 和 "speak": " 后面跟着的任何非引号/转义内容
                    thought_match = re.search(r'"thought"\s*:\s*"((?:[^"\\]|\\.)*)', full_text)
                    speak_match = re.search(r'"speak"\s*:\s*"((?:[^"\\]|\\.)*)', full_text)

                    # --- 1. 处理 Thought 碎片流 ---
                    if thought_match:
                        current_thought = thought_match.group(1).replace('\\n', '\n')
                        new_thought = current_thought[emitted_thought_len:]
                        if new_thought:
                            # 🌟 Task 4.8: 广播 Thought
                            await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_THOUGHT_STREAM, {
                                "chunk": new_thought,
                                "is_end": False
                            })
                            emitted_thought_len = len(current_thought)
                            
                            # 🌟 Task 1.6: 实时情绪感知与 VAD 阈值下发
                            # 简单正则匹配 Mood: Xxx
                            mood_match = re.search(r'Mood:\s*([A-Za-z]+)', new_thought, re.IGNORECASE)
                            if mood_match:
                                mood = mood_match.group(1).upper()
                                vad_sensitivity = 0.5 # Default Neutral
                                
                                if mood in ["ANGRY", "FOCUS", "EXCITED", "HAPPY"]:
                                    vad_sensitivity = 0.9 # 高唤醒 -> 高灵敏度 (-58dB)
                                elif mood in ["SAD", "SLEEPY", "TIRED"]:
                                    vad_sensitivity = 0.2 # 低唤醒 -> 低灵敏度 (-44dB)
                                
                                logger.info(f"🎭 感知到 AI 情绪: {mood} -> VAD Sensitivity: {vad_sensitivity}")
                                # 🌟 Task 4.8: 广播情绪
                                await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_EMOTION_SHIFT, {
                                    "ai_mood_score": 0, # 暂留
                                    "live2d_expression": mood.lower(),
                                    "live2d_motion": "nod",
                                    "vad_sensitivity": vad_sensitivity
                                })

                    # --- 2. 处理 Speak 碎片流并【标点截断】 ---
                    if speak_match:
                        current_speak = speak_match.group(1).replace('\\n', '\n')
                        new_speak = current_speak[emitted_speak_len:]

                        if new_speak:
                            sentence_buffer += new_speak
                            emitted_speak_len = len(current_speak)

                            # 查找最后出现的标点符号作为安全切分点
                            match = re.search(r'(.*[。！？，,\.\!\?])(.*)', sentence_buffer, re.DOTALL)
                            if match:
                                ready_to_speak = match.group(1).strip()
                                sentence_buffer = match.group(2)  # 没念完的半句话留在缓冲区

                                if ready_to_speak:
                                    sentence_id += 1
                                    # 将切好的句子扔上 TTS 传送带，让协程去跑
                                    await tts_queue.put((sentence_id, ready_to_speak))

            # --- 流式接收完毕，大收尾 ---
            if self.current_task_id == task_id:
                # 1. 广播 thought 结束信号
                await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_THOUGHT_STREAM, {
                    "chunk": "",
                    "is_end": True
                })

                # 2. 清空缓冲区里没有标点符号结尾的最后几个字
                if sentence_buffer.strip():
                    sentence_id += 1
                    await tts_queue.put((sentence_id, sentence_buffer.strip()))

                # 3. 往队列扔一个 None 作为毒药，通知 TTS 消费者安全下线
                await tts_queue.put(None)

                # 4. 挂起等待所有 TTS 任务合成完毕
                await tts_task
                if not mask_task.done():
                    mask_task.cancel()
                with suppress(asyncio.CancelledError):
                    await mask_task

                # 5. 【微观补齐】发送对话结束的空包，释放前端状态机
                if self.current_task_id == task_id:
                    await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_TTS_AUDIO, {
                        "audio_b64": "",
                        "sync_text": "",
                        "sentence_id": -1,
                        "is_reply_end": True,
                        "is_filler_audio": False
                    })
                    logger.info("✅ 这一轮对话彻底结束，状态重置！")

        except Exception as e:
            logger.error(f"💥 认知链路崩溃: {str(e)}")
            await self.send_message(msg_id, MessageType.SERVER_ERROR, {"message": "大脑神经元连接超时"})
    async def handle_message(self, raw_data: str):
        try:
            # 1. 第一步永远是先解析 JSON 信封
            envelope = WsMessage(**json.loads(raw_data))
            msg_type = envelope.type
            payload = envelope.payload
            msg_id = envelope.msg_id
        except Exception as e:
            logger.error(f"解析失败，非合法 JSON: {e}")
            return

        # 2. 优先处理高优先级的紧急打断信号
        if msg_type == MessageType.CLIENT_INTERRUPT:
            logger.warning(f"🛑 收到紧急打断信号: {payload.get('reason')}")
            # 刷新任务 ID，让所有正在进行的推理和 TTS 变成“废弃任务”
            self.current_task_id = str(uuid.uuid4())
            return

        # 3. 处理鉴权
        if not self.is_authenticated and msg_type == MessageType.CLIENT_AUTH_HANDSHAKE:
            if payload.get("access_token") == "neural_link_secret_2026":
                self.is_authenticated = True
                logger.info("✅ 鉴权成功")
            return

        # 🌟 Task 4.8: 处理主控权抢占
        if msg_type == MessageType.CLIENT_REQUEST_MASTER:
            NeuralLinkEngine._current_master_id = self.client_id
            logger.info(f"👑 客户端 {self.client_id} 抢占了主控权")
            await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_MASTER_GRANT, {
                "master_client_id": self.client_id,
                "timestamp": int(time.time() * 1000)
            })
            return

        # 4. 处理音频流 (🌟 Task 4.8: 增加互斥校验)
        if msg_type == MessageType.CLIENT_AUDIO_CHUNK:
            # 如果当前没有 Master，或者发送者不是 Master，丢弃音频
            if NeuralLinkEngine._current_master_id and self.client_id != NeuralLinkEngine._current_master_id:
                # logger.debug("🔇 忽略非 Master 节点的音频流")
                return

            chunk = ClientAudioChunk(**payload)
            if chunk.audio_b64:
                self.audio_buffer.append(base64.b64decode(chunk.audio_b64))

            if chunk.is_last:
                logger.info("🎤 录音接收完毕，开始 ASR...")
                full_audio = b"".join(self.audio_buffer)
                self.audio_buffer = [] # 绝对清空

                if len(full_audio) == 0:
                    return

                # 生成新任务的 ID
                self.current_task_id = str(uuid.uuid4())
                task_id = self.current_task_id

                try:
                    res = await asyncio.to_thread(process_and_recognize, full_audio)

                    # 守卫 0：如果 ASR 推理期间用户按了打断，直接丢弃识别结果
                    if self.current_task_id != task_id:
                        logger.info("任务已作废，丢弃 ASR 结果")
                        return

                    clean_text = re.sub(r'<\|.*?\|>', '', res[0]['text']).strip()
                    
                    # 🌟 Task 1.5: 提取 SenseVoiceSmall 的情感标签
                    emotion_tags = re.findall(r'<\|(.*?)\|>', res[0]['text'])
                    emotion_context = ""
                    if emotion_tags:
                        # 过滤掉非情感标签 (如 zh, itn 等)
                        valid_emotions = [tag for tag in emotion_tags if tag in ["HAPPY", "SAD", "ANGRY", "NEUTRAL", "SIGH", "LAUGH"]]
                        if valid_emotions:
                            emotion_context = f" (用户状态: {', '.join(valid_emotions)})"
                            logger.info(f"🎭 感知到副语言: {valid_emotions}")

                    logger.info(f"👂 听到了: {clean_text}{emotion_context}")

                    # 🌟 Task 4.8: 广播 ASR 结果
                    await NeuralLinkEngine.broadcast_message(msg_id, MessageType.SERVER_ASR_RESULT, {
                        "text": clean_text + emotion_context, # 将情感拼接到文本后，让前端也能看见
                        "is_valid_speech": len(clean_text) > 0
                    })


                except Exception as e:
                    logger.error(f"感知链路故障: {e}")
 # 5. 处理前端发来的带记忆的对话请求
        if msg_type == MessageType.CLIENT_TEXT_REQUEST:
            self.current_task_id = str(uuid.uuid4())
            task_id = self.current_task_id

            user_text = payload.get("text", "")
            chat_history = payload.get("chat_history", [])

            await self.run_llm_inference(msg_id, user_text, chat_history, task_id)


# ==========================================
# 3. FastAPI 路由
# ==========================================
app = FastAPI()


@app.on_event("startup")
async def warmup_latency_mask_audio():
    logger.info(
        "⚙️ 延迟掩盖配置: threshold=%sms filler_text=%s timeout=%ss",
        LATENCY_MASK_THRESHOLD_MS,
        LATENCY_MASK_FILLER_TEXT,
        LATENCY_MASK_TTS_TIMEOUT_SEC,
    )
    await NeuralLinkEngine.warmup_filler_audio()


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    engine = NeuralLinkEngine(websocket)
    try:
        while True:
            await engine.handle_message(await websocket.receive_text())
    except WebSocketDisconnect:
        logger.info("🔌 客户端已断开连接")
        # 🌟 Task 4.8: 移除连接
        if engine in NeuralLinkEngine._connected_clients:
            NeuralLinkEngine._connected_clients.remove(engine)
