//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "D3D12HelloTriangle.h"
#include <CommCtrl.h>
#pragma comment(lib, "Comctl32.lib")


LRESULT CALLBACK ControlWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        D3D12HelloTriangle* renderer = (D3D12HelloTriangle*)cs->lpCreateParams;

        // store it in the window data for later
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)renderer);

        // Create combobox (same as before)...
        HWND hCombo = CreateWindowEx(
            0,
            WC_COMBOBOX,
            L"",
            CBS_DROPDOWNLIST | WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            20, 20, 200, 200,
            hwnd,
            (HMENU)1001,
            ((LPCREATESTRUCT)lParam)->hInstance,
            NULL);

        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Flat");
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Normal");
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);

        break;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == CBN_SELCHANGE)
        {
            HWND combo = (HWND)lParam;
            int index = SendMessage(combo, CB_GETCURSEL, 0, 0);

            // retrieve renderer pointer
            D3D12HelloTriangle* renderer = (D3D12HelloTriangle*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            if (!renderer) break;

            if (index == 0)
                renderer->SetShadingMode(L"Flat");
            else
                renderer->SetShadingMode(L"Normal");
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

HWND CreateControlWindow(HINSTANCE hInstance, D3D12HelloTriangle* renderer)
{
    const wchar_t* className = L"ControlPanelClass";

    // Register a simple window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = ControlWndProc; // the previously defined procedure
    wc.hInstance = hInstance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0,
        className,
        L"Control Panel",
        WS_OVERLAPPEDWINDOW,
        50, 50, 300, 200,   // x, y, width, height
        nullptr, nullptr,
        hInstance,
        renderer);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    return hwnd;
}


_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	D3D12HelloTriangle sample(1280, 720, L"D3D12 Hello Triangle");

    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES; // includes combo box
    InitCommonControlsEx(&icex);

    HWND controlWindow = CreateControlWindow(hInstance, &sample);
	return Win32Application::Run(&sample, hInstance, nCmdShow);
}
