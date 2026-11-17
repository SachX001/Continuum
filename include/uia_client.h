#pragma once

#include <windows.h>
#include <UIAutomation.h>

namespace continuum {

class UiaClient {
public:
    UiaClient();
    ~UiaClient();

    bool initialize();
    IUIAutomationElement* element_from_window(HWND hwnd) const;

private:
    IUIAutomation* automation_ = nullptr;
};

}