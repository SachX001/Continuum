#include "../include/claude_windows_finder.h"

#include <vector>

namespace continuum {

namespace {

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam) {
    auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    wchar_t title[512]{};
    wchar_t class_name[256]{};

    GetWindowTextW(hwnd, title, 512);
    GetClassNameW(hwnd, class_name, 256);

    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);

    if (wcslen(title) == 0) {
        return TRUE;
    }

    windows->push_back({
        hwnd,
        title,
        class_name,
        process_id
    });

    return TRUE;
}

}

WindowInfo ClaudeWindowFinder::find() {
    std::vector<WindowInfo> windows;

    EnumWindows(enum_windows_proc,
                reinterpret_cast<LPARAM>(&windows));

    for (const auto& window : windows) {
        if (window.title.find(L"Claude") != std::wstring::npos) {
            return window;
        }
    }

    return {};
}

}