#include "Window.h"
#include "LAppModel.h"
#include <iostream>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "opengl32.lib")

NativeWindow::NativeWindow(int width, int height, const std::wstring& title)
    : m_width(width), m_height(height), m_title(title) {
}

NativeWindow::~NativeWindow() {
    if (m_hglrc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(m_hglrc);
    }
    if (m_hdc) {
        ReleaseDC(m_hwnd, m_hdc);
    }
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
    }
}

bool NativeWindow::Initialize() {
    // 1. 注册窗口类
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"NeuralLinkNativeWindow";

    if (!RegisterClassEx(&wc)) {
        std::cerr << "Failed to register window class" << std::endl;
        return false;
    }

    // 2. 创建窗口 (无边框 + 分层)
    // WS_POPUP: 无边框
    // WS_EX_TOPMOST: 置顶 (方便调试)
    // WS_EX_LAYERED: 用于支持透明点击穿透 (配合 DWM)
    // WS_EX_TRANSPARENT: 让窗口对鼠标事件透明 (但这会让整个窗口都穿透！) -> 不要用
    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_APPWINDOW, // 移除 WS_EX_LAYERED，因为它可能导致 OpenGL 渲染问题
        wc.lpszClassName,
        m_title.c_str(),
        WS_POPUP | WS_VISIBLE,
        (GetSystemMetrics(SM_CXSCREEN) - m_width) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - m_height) / 2,
        m_width,
        m_height,
        NULL,
        NULL,
        wc.hInstance,
        this
    );

    if (!m_hwnd) {
        std::cerr << "Failed to create window" << std::endl;
        return false;
    }

    // 设置分层窗口属性，使用 Alpha 通道
    // 注意：如果使用 OpenGL 渲染到 DWM 表面，通常不需要 UpdateLayeredWindow
    // 但是为了让 WS_EX_LAYERED 生效，可能需要调用 SetLayeredWindowAttributes
    // 这里设置 Key 为 0 (不使用)，Alpha 为 255 (完全不透明，由 OpenGL 控制 Alpha)，Flags 为 LWA_ALPHA
    // SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    // 实际上，对于 DWM 扩展帧，不需要 SetLayeredWindowAttributes，只需要 WS_EX_LAYERED 吗？
    // 让我们先试试只加 WS_EX_LAYERED。如果不显示，再加 SetLayeredWindowAttributes。
    
    // 经过测试，如果只加 WS_EX_LAYERED 而不调用 SetLayeredWindowAttributes，窗口可能不可见。
    // 让我们加上它。
    // SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    
    // 尝试使用 LWA_COLORKEY 来实现穿透？
    // 不，我们希望 Alpha 通道生效。
    // 实际上，如果使用了 DwmExtendFrameIntoClientArea，我们不需要 WS_EX_LAYERED 来实现透明。
    // 但是为了点击穿透，我们需要它。
    // 关键是：如果 WS_EX_LAYERED 被设置，HitTest 必须返回 HTTRANSPARENT 才能穿透。
    
    // 让我们尝试移除 SetLayeredWindowAttributes，只保留 WS_EX_LAYERED。
    // 如果窗口不可见，说明 OpenGL 无法直接绘制到 Layered Window。
    // 这种情况下，我们需要使用 UpdateLayeredWindow，但这很复杂且性能低。
    
    // 另一种方法：使用 SetWindowRgn。
    // 我们可以根据模型的轮廓创建一个 Region，然后设置给窗口。
    // 这能完美解决点击穿透，但需要每一帧更新 Region，性能开销大。
    
    // 回到 DWM。
    // 如果 DwmExtendFrameIntoClientArea 工作正常，视觉上是透明的。
    // 点击穿透问题通常是因为窗口仍然接收鼠标消息。
    // 如果 WM_NCHITTEST 返回 HTTRANSPARENT，系统应该将消息传递给下层窗口。
    
    // 让我们尝试移除 WS_EX_LAYERED，并确保 WM_NCHITTEST 逻辑正确。
    // 也许之前的 HitTest 逻辑还是有问题？
    // 如果 HitTest 返回 false，我们返回 HTTRANSPARENT。
    // 让我们加点日志来确认。
    
    // 暂时注释掉 SetLayeredWindowAttributes，看看效果。
    // SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);

    // 保存 this 指针以便在 WndProc 中使用
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

    // 3. 初始化 OpenGL
    if (!InitOpenGL()) {
        return false;
    }

    // 4. 启用 DWM 透明
    EnableTransparency();

    return true;
}

void NativeWindow::Show() {
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

bool NativeWindow::ProcessMessages() {
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_isRunning = false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return m_isRunning;
}

void NativeWindow::SwapBuffers() {
    ::SwapBuffers(m_hdc);
}

bool NativeWindow::InitOpenGL() {
    m_hdc = GetDC(m_hwnd);

    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_SUPPORT_COMPOSITION;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    if (pixelFormat == 0) {
        std::cerr << "ChoosePixelFormat failed" << std::endl;
        return false;
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) {
        std::cerr << "SetPixelFormat failed" << std::endl;
        return false;
    }

    // 创建临时上下文以初始化 GLEW
    HGLRC tempContext = wglCreateContext(m_hdc);
    wglMakeCurrent(m_hdc, tempContext);

    glewExperimental = GL_TRUE; // Enable experimental features (needed for core profile and some drivers)
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW initialization failed" << std::endl;
        return false;
    }

    // 这里可以升级到更高版本的 OpenGL (如 3.3 Core)，暂时使用兼容模式
    m_hglrc = tempContext; 
    
    // 开启混合以支持透明
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return true;
}

void NativeWindow::EnableTransparency() {
    // 使用 DWM 将玻璃效果扩展到整个客户区
    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);
}

void NativeWindow::RenderTest() {
    // 调试阶段：设置半透明红色背景，确认窗口位置
    // 正式发布时改为 (0.0f, 0.0f, 0.0f, 0.0f)
    glClearColor(0.2f, 0.0f, 0.0f, 0.5f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 绘制一个简单的旋转三角形
    static float angle = 0.0f;
    angle += 1.0f;

    glLoadIdentity();
    glRotatef(angle, 0.0f, 0.0f, 1.0f);

    glBegin(GL_TRIANGLES);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f); glVertex2f(0.0f, 0.5f);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f); glVertex2f(0.5f, -0.5f);
    glColor4f(0.0f, 0.0f, 1.0f, 1.0f); glVertex2f(-0.5f, -0.5f);
    glEnd();
}

LRESULT CALLBACK NativeWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NativeWindow* window = (NativeWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_NCHITTEST:
        if (window && window->m_model) {
            // Get mouse position in screen coordinates
            POINTS p = MAKEPOINTS(lParam);
            POINT pt = { p.x, p.y };
            
            // Convert to client coordinates
            ScreenToClient(hwnd, &pt);
            
            // Convert to OpenGL NDC (-1.0 to 1.0)
            // Note: Y is flipped in OpenGL (Up is positive), but Client Y is Down positive
            float ndcX = (float)pt.x / window->m_width * 2.0f - 1.0f;
            float ndcY = -((float)pt.y / window->m_height * 2.0f - 1.0f);
            
            // Hit Test
            if (window->m_model->HitTest(ndcX, ndcY)) {
                // Hit the model -> Allow dragging (HTCAPTION) or Interaction (HTCLIENT)
                // For now, let's allow dragging the whole model
                return HTCAPTION;
            } else {
                // Hit transparent area -> Pass through
                // 注意：如果窗口不是 Layered，HTTRANSPARENT 可能只穿透到同一进程的窗口。
                // 但是对于 DWM 扩展帧，这通常足够了。
                // 如果还是不行，可能需要返回 HTNOWHERE？不，那会丢弃消息。
                // 尝试返回 HTTRANSPARENT。
                return HTTRANSPARENT;
            }
        }
        // Fallback if model not ready
        return HTCAPTION; // 允许拖动整个窗口，如果模型没加载
        
    case WM_LBUTTONDOWN:
        // 如果 WM_NCHITTEST 返回 HTCAPTION，这里不会收到消息（系统处理拖动）。
        // 如果返回 HTCLIENT，这里会收到。
        // 如果返回 HTTRANSPARENT，这里不应该收到。
        break;
        
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
