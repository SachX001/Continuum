#pragma once

#include <windows.h>
#include <string>

namespace continuum {

struct WindowInfo {
    HWND hwnd;
    std::wstring title;
    std::wstring class_name;
    unsigned long process_id;
};

class ClaudeWindowFinder {
public:
    static WindowInfo find();
};

}