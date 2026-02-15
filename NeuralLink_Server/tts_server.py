import os
import sys
import io
import numpy as np
import soundfile as sf
import torch
from fastapi import FastAPI, Query
from fastapi.responses import Response
from contextlib import asynccontextmanager
import traceback


# ==========================================
# 1. 环境自举器 (Zerolan 风格的预检与自愈)
# ==========================================
class EnvironmentBootstrapper:
    @staticmethod
    def setup():
        print("🛡️ [Zerolan 风格] 正在进行环境自举与预检...")

        # 1. 路径定义
        SERVER_ROOT = r"E:\TOOL\AI\neurolike\NeuralLink_Server"
        GPT_SOVITS_ROOT = os.path.join(SERVER_ROOT, "GPT-SoVITS")
        GPT_LOGIC_DIR = os.path.join(GPT_SOVITS_ROOT, "GPT_SoVITS")

        # 2. 环境变量接管 (强制设置模型缓存路径，防止底层库乱跑)
        os.environ["HF_ENDPOINT"] = "https://hf-mirror.com"

        # 3. 动态系统路径注入
        if GPT_SOVITS_ROOT not in sys.path:
            sys.path.insert(0, GPT_SOVITS_ROOT)
        if GPT_LOGIC_DIR not in sys.path:
            sys.path.insert(0, GPT_LOGIC_DIR)

        os.chdir(GPT_SOVITS_ROOT)

        # 4. 🔥 核心自愈逻辑：提前创建所有容易报错的缓存文件夹
        pretrained_dir = os.path.join(GPT_LOGIC_DIR, "pretrained_models")
        # 针对 fast_langdetect 的致命报错，提前建好它的狗窝
        fast_langdetect_dir = os.path.join(pretrained_dir, "fast_langdetect")

        folders_to_ensure = [pretrained_dir, fast_langdetect_dir]
        for folder in folders_to_ensure:
            if not os.path.exists(folder):
                os.makedirs(folder, exist_ok=True)
                print(f"🔧 [自愈] 自动创建缺失的底层依赖目录: {folder}")

        print("✅ 环境预检通过，底层装甲已就绪。")
        return GPT_LOGIC_DIR, pretrained_dir


# 触发自举
GPT_LOGIC_DIR, PRETRAINED_DIR = EnvironmentBootstrapper.setup()

# ==========================================
# 2. 安全导入模型引擎
# ==========================================
try:
    from TTS_infer_pack.TTS import TTS, TTS_Config

    print("📦 成功挂载 GPT-SoVITS 纯净引擎")
except ImportError as e:
    print(f"❌ 引擎导入失败: {e}")
    sys.exit(1)

# ==========================================
# 3. 核心配置与全局单例
# ==========================================
GPT_MODEL = os.path.join(PRETRAINED_DIR, "s1v3.ckpt")
SOVITS_MODEL = os.path.join(PRETRAINED_DIR, "s2Gv3.pth")
REF_WAV = os.path.join(PRETRAINED_DIR, "ref_audio.wav")
REF_TEXT = "你好，小智，现在是2026年。"

tts_pipeline = None


# ==========================================
# 4. 生命周期管理
# ==========================================
@asynccontextmanager
async def lifespan(app: FastAPI):
    global tts_pipeline
    print("🎙️ 正在初始化硬件神经元...")
    try:
        config_path = os.path.join(GPT_LOGIC_DIR, "configs", "tts_infer.yaml")
        tts_config = TTS_Config(config_path)

        # 覆写配置路径
        tts_config.t2s_weights_path = GPT_MODEL
        tts_config.vits_weights_path = SOVITS_MODEL

        # 智能设备分配
        if torch.cuda.is_available():
            tts_config.device = "cuda"
            tts_config.is_half = True
            print("🚀 [GPU 模式] CUDA 可用，准备光速推理！")
        else:
            tts_config.device = "cpu"
            tts_config.is_half = False
            print("🐢 [CPU 模式] 未检测到 CUDA，降级为慢速推理。")

        # 实例化
        print("⏳ 正在加载 V3 权重...")
        tts_pipeline = TTS(tts_config)
        print("✅ 语音神经元彻底就绪，随时可合成音频！")

    except Exception as e:
        traceback.print_exc()
        print(f"❌ 引擎初始化失败: {e}")
    yield


app = FastAPI(lifespan=lifespan)


# ==========================================
# 5. API 路由层 (Facade 模式)
# ==========================================
@app.get("/tts")
async def generate_tts(text: str = Query(..., description="要合成的文字")):
    print(f"🔮 引擎接收合成任务: {text}")
    try:
        req = {
            "text": text,
            "text_lang": "zh",
            "ref_audio_path": REF_WAV,
            "prompt_text": REF_TEXT,
            "prompt_lang": "zh",
            "top_k": 5,
            "top_p": 1.0,
            "temperature": 1.0,
            "text_split_method": "cut5",
            "batch_size": 1,
            "speed_factor": 1.0,
            "split_bucket": True,
            "return_fragment": False
        }

        audio_data_list = []
        final_sr = 32000

        for sr, chunk in tts_pipeline.run(req):
            final_sr = sr
            audio_data_list.append(chunk)

        if not audio_data_list:
            raise ValueError("模型未生成任何声音信号")

        full_audio = np.concatenate(audio_data_list, axis=0)
        buffer = io.BytesIO()
        sf.write(buffer, full_audio, final_sr, format='WAV')

        print("⚡ 语音合成成功，已下发！")
        return Response(content=buffer.getvalue(), media_type="audio/wav")

    except Exception as e:
        traceback.print_exc()
        print(f"❌ 推理崩溃: {e}")
        return {"error": str(e)}


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="127.0.0.1", port=9880)