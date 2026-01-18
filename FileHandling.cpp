#include "D3D12HelloTriangle.h"
#include <Windows.h>
#include <commdlg.h>
#include <string>


std::wstring D3D12HelloTriangle::OpenFilePicker()
{
    wchar_t fileName[MAX_PATH] = {};
    fileName[0] = '\0';

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST;
    ofn.hwndOwner = Win32Application::GetHwnd();

    if (GetOpenFileNameW(&ofn))
        return fileName;

    return L"";
}

std::wstring D3D12HelloTriangle::SaveFilePicker()
{
    wchar_t fileName[MAX_PATH] = {};
    fileName[0] = '\0';

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    ofn.hwndOwner = Win32Application::GetHwnd();

    if (GetSaveFileNameW(&ofn))
        return fileName;

    return L"";
}

std::string D3D12HelloTriangle::WStringToUtf8(const std::wstring& wstr)
{
    if (wstr.empty())
        return {};

    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.c_str(),
        (int)wstr.size(),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    std::string result(sizeNeeded, 0);

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.c_str(),
        (int)wstr.size(),
        &result[0],
        sizeNeeded,
        nullptr,
        nullptr
    );

    return result;
}
