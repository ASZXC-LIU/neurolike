#pragma once

#include <windows.h>
#include <string>
#include <GL/glew.h>
#include <GL/wglew.h>

// Forward declaration
class LAppModel;

class NativeWindow {
public:
    NativeWindow(int width, int height, const std::wstring& title);
    ~NativeWindow();

    bool Initialize();
    void Show();
    bool ProcessMessages();
    void SwapBuffers();

    // 简单的渲染测试
    void RenderTest();
    
    // Set the model for hit testing
    void SetModel(LAppModel* model) { m_model = model; }

private:
    // 窗口过程回调
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 初始化 OpenGL 上下文 (WGL)
    bool InitOpenGL();

    // 启用 DWM 透明
    void EnableTransparency();

private:
    HWND m_hwnd = nullptr;
    HDC m_hdc = nullptr;
    HGLRC m_hglrc = nullptr;
    
    int m_width;
    int m_height;
    std::wstring m_title;
    bool m_isRunning = true;
    
    LAppModel* m_model = nullptr;
};
