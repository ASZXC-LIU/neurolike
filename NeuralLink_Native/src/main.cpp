#include "Window.h"
#include "LAppAllocator.h"
#include "LAppModel.h"
#include <CubismFramework.hpp>
#include <iostream>
#include <fstream>
#include <sys/stat.h> // for stat

// Global allocator
static LAppAllocator allocator;
static Csm::CubismFramework::Option option;

// File Loader Implementation
static Csm::csmByte* LoadFileAsBytes(const std::string filePath, Csm::csmSizeInt* outSize)
{
    // filePath is relative to current working directory
    // But SDK might pass "FrameworkShaders/..."
    
    std::string path = filePath;
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    
    if (!file.is_open()) {
        // Try adding Debug/ prefix
        path = "Debug/" + filePath;
        file.open(path.c_str(), std::ios::binary | std::ios::ate);
    }
    
    if (!file.is_open()) {
        // Try adding ../ prefix
        path = "../" + filePath;
        file.open(path.c_str(), std::ios::binary | std::ios::ate);
    }

    if (!file.is_open()) {
        std::cerr << "[LoadFile] Failed to open file: " << filePath << std::endl;
        return NULL;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    Csm::csmByte* buffer = static_cast<Csm::csmByte*>(allocator.Allocate(static_cast<Csm::csmSizeInt>(size)));
    if (!buffer) {
        return NULL;
    }
    
    file.read(reinterpret_cast<char*>(buffer), size);
    
    if (outSize) {
        *outSize = static_cast<Csm::csmSizeInt>(size);
    }
    
    return buffer;
}

static void ReleaseBytes(Csm::csmByte* buffer)
{
    allocator.Deallocate(buffer);
}

int main() {
    // 隐藏控制台窗口 (可选，调试阶段建议保留)
    // ShowWindow(GetConsoleWindow(), SW_HIDE);

    std::cout << "NeuralLink Native Client (Win32 + OpenGL) Starting..." << std::endl;

    // Initialize Live2D Cubism SDK
    option.LogFunction = [](const char* message) {
        std::cout << "[Live2D] " << message << std::endl;
    };
    option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Verbose;
    option.LoadFileFunction = LoadFileAsBytes;
    option.ReleaseBytesFunction = ReleaseBytes;

    if (Csm::CubismFramework::StartUp(&allocator, &option)) {
        std::cout << "Live2D Cubism SDK StartUp Success" << std::endl;
    } else {
        std::cerr << "Live2D Cubism SDK StartUp Failed" << std::endl;
        return -1;
    }

    Csm::CubismFramework::Initialize();
    std::cout << "Live2D Cubism SDK Initialized" << std::endl;

    // 创建 800x600 的透明窗口
    NativeWindow window(800, 600, L"NeuralLink Native Client");

    if (!window.Initialize()) {
        std::cerr << "Failed to initialize window" << std::endl;
        return -1;
    }

    window.Show();

    // 打印 OpenGL 版本信息
    std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;
    std::cout << "Vendor: " << glGetString(GL_VENDOR) << std::endl;
    std::cout << "Renderer: " << glGetString(GL_RENDERER) << std::endl;

    // Load Model
    LAppModel model;
    
    // 自动探测资源路径
    std::string modelDir;
    std::string modelFile = "hiyori_free_t08.model3.json";
    
    struct stat info;
    if (stat("Resources", &info) == 0) {
        modelDir = "Resources/Hiyori/runtime";
    } else if (stat("Debug/Resources", &info) == 0) {
        modelDir = "Debug/Resources/Hiyori/runtime";
    } else if (stat("../Resources", &info) == 0) {
        modelDir = "../Resources/Hiyori/runtime";
    } else {
        std::cerr << "[Error] Could not find Resources directory!" << std::endl;
        modelDir = "Resources/Hiyori/runtime"; // Fallback
    }
    
    std::cout << "Loading model from: " << modelDir << "/" << modelFile << std::endl;
    model.LoadAssets(modelDir, modelFile);

    // Set model to window for hit testing
    window.SetModel(&model);

    // Setup View Matrix
    Csm::CubismMatrix44 viewMatrix;
    
    // 调整缩放和位置，确保模型可见
    float canvasWidth = model.GetCanvasWidth();
    float canvasHeight = model.GetCanvasHeight();
    
    std::cout << "Model Canvas Size: " << canvasWidth << "x" << canvasHeight << std::endl;

    if (canvasWidth == 0 || canvasHeight == 0) {
        std::cout << "Warning: Canvas size is 0, using default 2400x3200" << std::endl;
        canvasWidth = 2400.0f;
        canvasHeight = 3200.0f;
    }
    
    if (canvasWidth > 0 && canvasHeight > 0) {
        // 保持宽高比，缩放到窗口内
        // 窗口是 800x600 (4:3)
        // Canvas 可能是 2000x3000 (2:3)
        // 我们希望模型高度填满窗口高度 (或者宽度填满宽度)
        
        // OpenGL NDC 是 -1 到 1，总宽度 2.0，总高度 2.0
        // 模型坐标是 0 到 CanvasWidth, 0 到 CanvasHeight (或者中心是 0,0?)
        // Live2D 模型坐标通常是以 Canvas 中心为原点吗？不，通常左下角是 (0,0) 或者中心是 (0,0)。
        // CubismModel::GetCanvasWidth() 返回的是逻辑宽度。
        
        // 实际上，Live2D 默认坐标系是：
        // X: -CanvasWidth/2 到 +CanvasWidth/2
        // Y: -CanvasHeight/2 到 +CanvasHeight/2
        // (如果使用了 CubismModelMatrix)
        
        // 让我们先尝试简单的缩放：
        // 将 Canvas 映射到 NDC (-1, 1)
        // scaleX = 2.0 / CanvasWidth
        // scaleY = 2.0 / CanvasHeight
        // 但是要保持宽高比，取较小的那个缩放比例
        
        float scaleX = 2.0f / canvasWidth;
        float scaleY = 2.0f / canvasHeight;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;
        
        // 稍微缩小一点，留点边距
        scale *= 0.9f;
        
        viewMatrix.Scale(scale, scale);
        
        // 尝试平移到中心 (假设模型原点在左下角)
        viewMatrix.Translate(-canvasWidth / 2.0f, -canvasHeight / 2.0f);
        
        std::cout << "Canvas Size: " << canvasWidth << "x" << canvasHeight << std::endl;
        std::cout << "View Scale: " << scale << std::endl;
    } else {
        viewMatrix.Scale(1.0f, 1.0f); 
    }
    
    // 主循环
    while (window.ProcessMessages()) {
        // Clear background
        // 调试阶段：半透明红色
        // glClearColor(0.2f, 0.0f, 0.0f, 0.5f);
        // 正式发布：全透明
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // 设置视口
        glViewport(0, 0, 800, 600);

        // Update and Draw Model
        glDisable(GL_CULL_FACE); // 尝试禁用剔除
        glEnable(GL_TEXTURE_2D); // 确保启用纹理
        glEnable(GL_BLEND);      // 确保启用混合
        
        // 清除之前的错误
        while (glGetError() != GL_NO_ERROR);
        
        model.SetViewMatrix(viewMatrix); // Update View Matrix for HitTest
        model.Update();
        model.Draw(viewMatrix);
        
        /* 移除测试三角形
        // 绘制一个测试三角形，确认 GL 几何绘制正常
        glLoadIdentity();
        glBegin(GL_TRIANGLES);
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f); glVertex2f(0.0f, 0.5f);
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f); glVertex2f(0.5f, -0.5f);
        glColor4f(0.0f, 1.0f, 0.0f, 1.0f); glVertex2f(-0.5f, -0.5f);
        glEnd();
        */
        
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "OpenGL Error: " << err << std::endl;
        }

        window.SwapBuffers();
        
        // 简单的帧率控制 (~60 FPS)
        Sleep(16);
    }

    // Cleanup Live2D
    Csm::CubismFramework::Dispose();
    std::cout << "Live2D Cubism SDK Disposed" << std::endl;

    return 0;
}
