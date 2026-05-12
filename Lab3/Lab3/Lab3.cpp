// Lab3.cpp : Определяет точку входа для приложения.
//

#include "framework.h"
#include "Lab3.h"
#include "Render.h"
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <iostream>
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"

using Microsoft::WRL::ComPtr;

#pragma comment (lib, "d3d11.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "dxguid.lib")

#define MAX_LOADSTRING 100

WCHAR szTitle[MAX_LOADSTRING] = L"Lab 3. Aloy & Solomatov";
WCHAR szWindowClass[MAX_LOADSTRING] = L"Lab 3. Aloy & Solomatov";

ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

std::unique_ptr<Render> g_Render;

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        OutputDebugString(_T("Error in Init\n"));
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAB3));

    MSG msg = {};
    bool exit = false;
    while (!exit)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                exit = true;
            }
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        else if (g_Render)
        {
            g_Render->RenderStart();
        }
    }

    if (g_Render) {
        g_Render.reset();
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAB3));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    RECT rc = { 0, 0, 800, 600 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd)
    {
        OutputDebugString(_T("Error in Create window\n"));
        return FALSE;
    }

    g_Render = std::make_unique<Render>(hWnd);
    if (FAILED(g_Render->Init(szTitle, szWindowClass)))
    {
        OutputDebugString(_T("Error in Init Renderer\n"));
        g_Render.reset();
        return FALSE;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

void HandleResize(WPARAM wParam)
{
    if (g_Render != nullptr && wParam != SIZE_MINIMIZED)
    {
        g_Render->Resize();
    }
}

bool IsCameraControlKey(WPARAM wParam)
{
    switch (wParam)
    {
    case 'W':
    case 'S':
    case 'A':
    case 'D':
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_ADD:
    case VK_SUBTRACT:
    case VK_SPACE:
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case 'R':
    case 'F':
    case 'T':
    case 'G':
        return true;
    default:
        return false;
    }
}

void HandleKeyDown(WPARAM wParam, LPARAM lParam)
{
    if (IsCameraControlKey(wParam))
    {
        g_Render->UpdateCamera(wParam, lParam);
    }
    else if (wParam == VK_ESCAPE)
    {
        PostQuitMessage(0);
    }
}



LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
            return true;
    }

    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        HandleResize(wParam);
        return 0;

    case WM_KEYDOWN:
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureKeyboard)
                return 0;
        }
        HandleKeyDown(wParam, lParam);
        return 0;

    case WM_MOUSEMOVE:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        if (ImGui::GetCurrentContext() != nullptr)
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureMouse)
                return 0;
        }
        g_Render->HandleMouse(message, lParam);
        return 0;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}


