#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include <filesystem>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace std;
namespace fs = std::filesystem;

HWND hEditLog = NULL, hSourceEdit = NULL, hTargetEdit = NULL;

void LogMessage(const wstring& msg) {
    if (hEditLog) {
        SendMessageW(hEditLog, EM_SETSEL, -1, -1);
        SendMessageW(hEditLog, EM_REPLACESEL, FALSE, (LPARAM)msg.c_str());
        SendMessageW(hEditLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    }
}

bool IsRunAsAdmin() {
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;
    if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0,0,0,0,0,0, &AdministratorsGroup)) return false;
    BOOL isAdmin = FALSE;
    CheckTokenMembership(NULL, AdministratorsGroup, &isAdmin);
    FreeSid(AdministratorsGroup);
    return isAdmin != FALSE;
}

bool RemoveDirectoryRecursive(const wstring& path) {
    try { fs::remove_all(path); return true; } catch (...) { return false; }
}

wstring BrowseFolder(HWND hwnd, const wchar_t* title) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = hwnd;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_NEWDIALOGSTYLE | BIF_EDITBOX | BIF_RETURNONLYFSDIRS;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return L"";
    wchar_t path[MAX_PATH] = {0};
    if (SHGetPathFromIDListW(pidl, path)) {
        CoTaskMemFree(pidl);
        return wstring(path);
    }
    CoTaskMemFree(pidl);
    return L"";
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    HMODULE hComctl32 = LoadLibraryW(L"comctl32.dll");
    if (hComctl32) {
        typedef HRESULT(WINAPI* PFN)(INITCOMMONCONTROLSEX*);
        PFN pInit = (PFN)GetProcAddress(hComctl32, "InitCommonControlsEx");
        if (pInit) {
            INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
            pInit(&icc);
        }
    }

    const wchar_t CLASS_NAME[] = L"NodeModulesLinkerClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Node Modules 符号链接工具",
        WS_OVERLAPPEDWINDOW & ～WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 520, 420, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void OnBrowseSource(HWND hwnd) {
    wstring dir = BrowseFolder(hwnd, L"选择共享的 node_modules 文件夹");
    if (!dir.empty()) SetWindowTextW(hSourceEdit, dir.c_str());
}

void OnBrowseTarget(HWND hwnd) {
    wstring dir = BrowseFolder(hwnd, L"选择项目根目录（将在其中创建 node_modules）");
    if (!dir.empty()) {
        dir += L"\\node_modules";
        SetWindowTextW(hTargetEdit, dir.c_str());
    }
}

void OnCreateLink(HWND hwnd) {
    wchar_t source[32768] = {0}, target[32768] = {0};
    GetWindowTextW(hSourceEdit, source, _countof(source));
    GetWindowTextW(hTargetEdit, target, _countof(target));

    wstring src(source), tgt(target);
    if (src.empty() || tgt.empty()) {
        MessageBoxW(hwnd, L"请填写源路径和目标路径！", L"输入错误", MB_ICONWARNING);
        return;
    }
    if (!fs::exists(src)) {
        MessageBoxW(hwnd, (L"源路径不存在：\n" + src).c_str(), L"路径错误", MB_ICONERROR);
        return;
    }
    if (fs::exists(tgt)) {
        int ret = MessageBoxW(hwnd, (L"目标路径已存在：\n" + tgt + L"\n\n是否删除并重新创建链接？").c_str(),
            L"确认覆盖", MB_YESNO | MB_ICONQUESTION);
        if (ret == IDYES) {
            if (!RemoveDirectoryRecursive(tgt)) {
                MessageBoxW(hwnd, L"无法删除现有路径！", L"删除失败", MB_ICONERROR);
                return;
            }
            LogMessage(L"✅ 已删除现有路径：" + tgt);
        } else return;
    }
    if (CreateSymbolicLinkW(tgt.c_str(), src.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        LogMessage(L"✅ 符号链接创建成功！");
        MessageBoxW(hwnd, L"符号链接已创建成功！", L"成功", MB_ICONINFORMATION);
    } else {
        DWORD err = GetLastError();
        wstring msg = L"❌ 创建失败！错误代码：" + to_wstring(err);
        LogMessage(msg);
        LogMessage(L"请确保：");
        LogMessage(L"1. 以管理员身份运行程序");
        LogMessage(L"2. 路径无非法字符");
        LogMessage(L"3. 目标磁盘有写入权限");
        MessageBoxW(hwnd, (L"创建失败！错误码：" + to_wstring(err)).c_str(), L"失败", MB_ICONERROR);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

        CreateWindowW(L"STATIC", L"源 node_modules 文件夹（共享）", WS_CHILD | WS_VISIBLE, 10, 10, 300, 20, hwnd, NULL, NULL, NULL);
        hSourceEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 35, 400, 25, hwnd, NULL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 420, 35, 80, 25, hwnd, (HMENU)1, NULL, NULL);
        SendMessageW(hSourceEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowW(L"STATIC", L"目标 node_modules 路径（项目内）", WS_CHILD | WS_VISIBLE, 10, 70, 300, 20, hwnd, NULL, NULL, NULL);
        hTargetEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 10, 95, 400, 25, hwnd, NULL, NULL, NULL);
        CreateWindowW(L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 420, 95, 80, 25, hwnd, (HMENU)2, NULL, NULL);
        SendMessageW(hTargetEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowW(L"BUTTON", L"创建符号链接", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 10, 130, 150, 30, hwnd, (HMENU)3, NULL, NULL);
        SendMessageW(GetDlgItem(hwnd, 3), WM_SETFONT, (WPARAM)hFont, TRUE);

        hEditLog = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
            10, 170, 490, 200, hwnd, NULL, NULL, NULL);
        SendMessageW(hEditLog, WM_SETFONT, (WPARAM)hFont, TRUE);

        if (!IsRunAsAdmin()) {
            LogMessage(L"⚠️ 警告：未以管理员身份运行！创建符号链接可能失败。");
            LogMessage(L"建议右键程序 → “以管理员身份运行”。\r\n");
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) OnBrowseSource(hwnd);
        else if (LOWORD(wParam) == 2) OnBrowseTarget(hwnd);
        else if (LOWORD(wParam) == 3) OnCreateLink(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
