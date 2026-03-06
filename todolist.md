### 🚀 第一阶段：流式管线与大脑本地化 (极致首字延迟)

*当前痛点：API 调用是“想完一整段才说”，首字延迟（TTFA）可能高达 3-5 秒，且依赖云端，不符合 3060 离线推理的愿景。*

- **[ ] Task 1.1：本地大模型基座替换 (3060 算力释放)**

  - 在 3060 节点部署 `vLLM` 或 `Ollama`，加载量化版的 `Qwen2.5-7B-Instruct` 或 `Llama-3-8B`。
  - 修改 `main.py` 的大模型调用逻辑，剔除 Gemini，全面转向本地 OpenAI 兼容 API 请求，实现**完全断网可用**。
- [x] Task 1.2：大模型流式输出 (Streaming) 改造

  - 将大模型请求设置为 `stream=True`。
  - 编写一个**流式 JSON 解析器**：因为大模型流式吐出的是不完整的 JSON 字符串，需要使用正则或缓冲池，实时提取 `thought` 和 `speak` 字段的碎片。
- [x] Task 1.3：标点符号截断 (Semantic Chunking) 与并发 TTS

  - 当提取出的 `speak` 文本流中遇到 `[。！？，,.]` 时，立刻触发截断。
  - 将截断的这句话**异步**发给 9880 端口进行 TTS 合成。
  - 合成完毕后立刻通过 WebSocket 推送给前端 `SERVER_TTS_AUDIO`，实现“边想、边合成、边播放”的无缝衔接，将延迟压进 1 秒内。
- [ ] Task 1.4：延迟掩盖 (Latency Masking - 拟人化处理)**

  - 在 `main.py` 中增加计时器：如果 ASR 识别完毕后，大模型思考时间超过 600ms，自动向前端下发一段预生成的音频（如“嗯…”、“让我想想…”），消除用户的等待焦虑。
- **[ ] Task 1.5：SSML 动态标记注入与副语言感知 (语调拟人)**

  - *遗漏点*：SRS 中提到人类生气和温柔的语速不同，以及用户叹气、笑声的捕捉。
  - *待办*：在 `SenseVoiceSmall` 提取出用户的情感标签（叹气、犹豫）后，作为独立字段喂给大模型；要求大模型在 `speak` 中注入 SSML 指令（如 `<1.2x>` 或 `<+2st>`），实现动态语速和音高。
- **[ ] Task 1.6：基于情绪的动态 VAD 阈值调整**
  - *遗漏点*：SRS 提出“处于极度关注或生气状态时，应调高 ASR 灵敏度”。
  - *待办*：当前端收到 `SERVER_EMOTION_SHIFT` 且情绪值为极值时，动态调整前端 `MediaRecorder` 或底层 `AudioContext` 的 VAD 能量阈值。
- **[ ] Task 1.7：动态 Prompt 截断指令注入**
  - **待办**：在大脑推理（`run_llm_inference`）时，需要从本地配置中读取字数限制参数，并动态拼接到 System Prompt 中（例如：“注意：你的 thought 思考过程严格控制在 30 字以内”），从源头压榨首字延迟,减少token开支。

### 🧠 第二阶段：记忆矩阵与端侧存储 (赋予灵魂)

*当前痛点：代码是无状态的，重启或刷新后，AI 就会变成失忆的金鱼。*

- **[ ] Task 2.1：客户端 SQLite 短期记忆落盘**
  - 在 Node.js (Electron) 层引入 `better-sqlite3`。
  - 编写 `HistoryManager` 类，拦截所有的 `CLIENT_TEXT_REQUEST` 和接收到的 `SERVER_TTS_AUDIO` 文本，双向存入本地数据库。

- [x] Task 2.2：上下文滑动窗口

  - 修改客户端请求协议，每次发语音请求时，附带最近 N 轮的 `chat_history` 发给 3060 大脑。

- **[ ] Task 2.3：记忆摘要坍缩 (Summarization)**

  - 编写后台任务：当记忆轮数超过阈值，且状态机处于 `idle` 时，调用大模型将旧对话压缩为“语义摘要”，防止后续请求 Token 溢出。

- **[ ] Task 2.4：长期向量记忆 (RAG) 基础搭建**
  - 在客户端或服务端部署 ChromaDB 或 Qdrant。
  - 基于时效与情感双加权算法，实现 Top-K 记忆片段的召回，作为 `rag_context` 塞入 Prompt。

- **[ ] Task 2.5：端到端 AES 加密与助记词系统 (极致隐私)**

  - *遗漏点*：SRS 强调了存储和云同步前的绝对加密，以及防丢失的助记词。
  - *待办*：引入 Node.js `crypto` 模块。在第一次启动时生成加密钱包级别的“助记词”供用户离线保存；所有落盘到 SQLite 的敏感数据和向远程同步的碎片，必须先通过助记词派生的 AES-256 密钥进行加密。

  **[ ] Task 2.6：记忆逻辑冲突自修复与“静默更新”**

  - *遗漏点*：当旧记忆与新事实冲突时，避免频繁向用户确认造成骚扰。
  - *待办*：在 RAG 检索层加入判定逻辑，当检索得分 $Score_{final}$ 接近且语义冲突时，触发静默覆写（调高新事实权重，抹除旧事实）。

- **[ ] Task 2.7：WebDAV / S3 后台静默同步进程**

  - **待办**：在 Electron 主进程中编写一个基于 Cron 的后台 Worker。定时将本地加密后的 SQLite 快照和向量库冷数据块，通过 WebDAV/S3 协议静默推送到远端私有云，保证“重装系统后可通过助记词一键拉取恢复”。

### 🎭 第三阶段：多模态感知与动态表现 (Live2D 联动)

*当前痛点：目前只有一个麦克风按钮和控制台，缺乏二次元或具象化的物理实体。*

- **[ ] Task 3.1：Live2D WebGL SDK 渲染引擎集成**
  - 在 Vue 3 项目中引入 `pixi.js` 和 `live2d-cubism-core`，在画布中渲染模型。
- **[ ] Task 3.2：音频振幅驱动口型 (Lip-Sync)**
  - 在前端的 `AudioReceiver.vue` 中，利用 `AudioContext` 实时提取正在播放音频的频率波形（Volume DB）。
  - 将波形数据映射到 Live2D 模型的 `ParamMouthOpenY` 参数，实现“音在嘴动”的精准对口型。
- **[ ] Task 3.3：内心独白驱动情感表达**
  - 利用后端传回的 `SERVER_EMOTION_SHIFT` 或 `thought` 字段中的 `Mood_Score`，自动切换 Live2D 模型的表情组合（如：生气、害羞、微笑）。
- **[ ] Task 3.4：音频播放进度与 Live2D 动作帧的时间戳对齐**
  - **待办**：不能仅靠 `volume_db` 驱动。需要在前端的 `AudioReceiver` 播放音频时，获取 `audio.currentTime`，并结合后端传回的 `sentence_id` 与时间戳，对 Live2D 的动作插值进行毫秒级的偏移补偿，彻底解决网络抖动导致的“音画撕裂”。

### 🛠️ 第四阶段：系统级掌控与主动意识 (Agent 终极形态)

*当前痛点：依然是“一问一答”的对讲机模式，AI 无法自己主动发起话题，也无法操控你的电脑。*

- **[ ] Task 4.1：系统节拍器 (System Tick) 与自主广播**
  - 在客户端实现定时任务（Cron），每隔一段时间（如 1 分钟）向总线广播 `CLIENT_SYSTEM_TICK`。
  - Payload 包含：`current_time`, `active_window_title`, `user_idle_seconds`, `battery_level`。
  - 服务端逻辑：收到 Tick 后，LLM 进行快速推理（System Prompt 需包含“仅在必要时说话”的约束）。如果决定说话，生成 `speak` 内容；否则返回空。

- **[ ] Task 4.2：视觉与环境感知 (Context Sensing)**
  - **屏幕感知**：集成 `active-win` 或类似库，实时获取当前前台应用的名称和标题。
  - **剪贴板监听**：监听剪贴板文本变化，当复制了特定格式（如代码段）时，主动询问“需要解释这段代码吗？”（需设置开关，防止烦人）。
  - **无感交互**：实现 Live2D 的“眼神跟随”功能（根据鼠标位置移动眼球），以及“闲置动作”（用户不理它时，自动播放打哈欠、趴桌子动画）。

- **[ ] Task 4.3：意图路由 (Intent Router) 与函数调用 (Function Calling)**

  - 大模型返回时，如果判断是系统级指令（如“打开网易云”），不走 TTS 闲聊管线，而是发送 `SERVER_INTENT_CALL`。

- **[ ] Task 4.3：本地执行层 (Input Mimicry) 接入**

  - 使用 `nut.js` 编写执行插件，订阅意图广播。收到打开软件或输入密码的指令后，模拟真实的非均匀鼠标移动和键盘敲击。

- **[ ] Task 4.4：脊髓反射路由 (Semantic Router - 毫秒级拦截)**

  - *遗漏点*：SRS 中明确规划了绕过 3060 大模型的本地极速拦截（< 5ms）。
  - *待办*：在 Node.js 客户端增加本地轻量级文本匹配（正则或本地极小向量库），遇到如“闭嘴”、“调大音量”等指令，不走网络请求，直接在本地 EventBus 触发执行层。

  **[ ] Task 4.5：“一键闭嘴”的全局物理熔断开关**

  - *遗漏点*：优先级高于一切的打断机制。
  - *待办*：利用 Electron 的 `globalShortcut` 模块，注册一个系统级系统级快捷键（如 `Ctrl+Alt+M`）。一旦按下，强行清空所有队列，挂起状态机，切断音频输出。

  **[ ] Task 4.6：情绪惯性缓存优化 (性能优化)**

  - *遗漏点*：避免大模型重复进行相同的 `thought` 推演。
  - *待办*：如果连续 3 轮交互检测到上下文环境和情绪波动极小，复用上一次的 `thought` 策略模板，跳过部分复杂推理，节约本地 VRAM 和计算时间。

- **[ ] Task 4.7：垂直生态插件矩阵开发**

  - **待办**：利用微内核 EventBus，分别实现：
    1. **文件引擎**：对接你简历里的 Markdown/PDF 读写逻辑。
    2. **IM 挂载**：通过图像识别或无障碍接口读取消息并回复。
    3. **RCON 协议封装**：实现 Minecraft 等游戏的底层指令注入。

- **[ ] Task 4.8：多端协同与静音互斥 (Multi-Client Mutex)**
  - **服务端**：维护 `current_master_client_id`。当收到 `CLIENT_WAKE_UP` 或 `CLIENT_AUDIO_CHUNK` 时，校验是否为 Master。
  - **客户端**：实现 `Active/Passive` 状态切换。Passive 模式下，收到 `SERVER_TTS_AUDIO` 仅更新字幕和表情，不调用 `AudioContext.play()`。
  - **抢占机制**：Vue 端增加“获取控制权”按钮，点击后发送 `CLIENT_REQUEST_MASTER`，强制剥夺 C++ 端的发声权。

- **[ ] Task 4.9：Vue Client 形态重构 (Console Mode)**
  - **UI 分离**：将 Live2D 渲染区域独立为一个组件，增加“隐藏/显示”开关。
  - **默认行为**：Vue Client 启动时默认进入“控制台模式”（隐藏 Live2D，仅显示日志和设置），除非用户手动开启 Live2D 调试。
  - **多端共存**：确保 Vue Client (控制台) 和 C++ Client (桌宠) 可以同时连接 WebSocket，互不干扰（基于 Task 4.8 的互斥机制）。

### 🖥️ 第六阶段：C++ Native Client (沉浸式桌宠)

*目标：构建极致性能、完美透明、系统级集成的最终形态桌宠。*

- **[x] Task 6.1：开发环境搭建**
  - 安装 Visual Studio 2022 (C++ Desktop Workload)。
  - 配置 `vcpkg` 包管理器 (Manifest Mode)。
  - 集成 `Boost.Beast` (WebSocket), `nlohmann-json`, `OpenSSL`, `GLEW`, `GLFW`。
  - 验证 Live2D Cubism SDK for Native 路径配置。
  - 成功编译并运行 Hello World (JSON + Boost.Beast 环境检查)。

- **[ ] Task 6.2：透明窗口与渲染管线**
  - 基于 Win32 API 创建无边框窗口 (`WS_EX_LAYERED`)。
  - 集成 DWM API (`DwmExtendFrameIntoClientArea`) 实现 Alpha 通道透传。
  - 初始化 OpenGL 上下文，确保 `glClearColor(0,0,0,0)` 正确清除背景。

- **[ ] Task 6.3：交互穿透 (Hit Testing)**
  - 拦截 `WM_NCHITTEST` 消息。
  - 调用 Cubism SDK 的 `IsHit()` 方法判断鼠标是否点击在模型网格上。
  - 实现：点在模型上 -> `HTCLIENT`，点在空白处 -> `HTTRANSPARENT`。

- **[ ] Task 6.4：网络与音频集成**
  - 集成 `websocketpp` 或 `ixwebsocket`，实现与 Python Server 的全双工通信。
  - 集成 `miniaudio` 或 `PortAudio`，实现麦克风录制与 PCM 音频流播放。
  - 对齐 Vue 端的协议：发送 `CLIENT_AUDIO_CHUNK`，接收 `SERVER_TTS_AUDIO`。

- **[ ] Task 6.5：Live2D 动作驱动**
  - 解析 `SERVER_EMOTION_SHIFT`，切换模型表情 (`Expression`)。
  - 解析音频振幅 (`Volume DB`)，实时驱动 `ParamMouthOpenY` 实现口型同步。

- **[ ] Task 6.6：离线行为树 (Offline Behavior Tree)**
  - **机制**：当 WebSocket 断连时，启动本地简易行为树。
  - **功能**：响应鼠标点击（播放本地预设 WAV，如“大脑连接断开啦...”），随机播放呼吸/眨眼动画，避免模型僵死。

### **第七阶段：系统基建、防御与自我进化**

- **[ ] Task 5.1：mDNS 局域网零配置发现 (无感组网)**

  - *待办*：除了硬编码的 `ws://127.0.0.1:8000`。可以让用户选择 在 3060 服务端广播 mDNS 服务，在主力机客户端使用 `bonjour` 或 `multicast-dns` 自动发现局域网内的算力节点并静默直连。

- **[ ] Task 5.2：沙箱白名单机制 (防封号策略)**

  - *待办*：在 `nut.js` 执行鼠标/键盘注入前，增加进程检查器。如果是竞技游戏（如原神、CS2）处于前台，严格禁止任何模拟输入，防止触发反作弊机制，仅允许语音和视觉交互。

- **[ ] Task 5.3：灵魂数据集自动沉淀 (Fine-tuning 储备)**

  - *待办*：在 EventBus 的末端挂载一个静默的数据探针。将包含强烈情绪波动且交互成功的对话，自动清洗并格式化为 `{"instruction": "...", "output": "..."}` 的 JSONL 格式落盘，为未来你在 3060 上微调（LoRA）专属模型囤积高质量私有语料。

- **[ ] Task 5.4：全量参数化 GUI 设置面板**

  - *待办*：不再允许代码中出现任何“魔法数字”（如 500ms、情绪阈值 50、端口号）。用 Vue 3 编写一个配置抽屉，将模型路径、灵敏度、内心独白长度限制等参数全部暴露给用户实时调节。

  - 利用 Electron 的 `Tray` 模块开发**系统托盘**右键菜单，集成“物理熔断”和“状态重置”功能。

    在 GUI 设置面板中做一个“开发者模式”开关（比如连按 5 次版本号开启），将现在的 `MonologueDebugger.vue` 默认隐藏，仅在开发者模式下渲染。