#include "../include/uia_client.h"

#include <iostream>

namespace continuum {

UiaClient::UiaClient() = default;

UiaClient::~UiaClient() {
    if (automation_) {
        automation_->Release();
        automation_ = nullptr;
    }

    CoUninitialize();
}

bool UiaClient::initialize() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (FAILED(hr)) {
        std::cerr << "COM initialization failed.\n";
        return false;
    }

    hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation_)
    );

    if (FAILED(hr)) {
        std::cerr << "UI Automation initialization failed.\n";
        CoUninitialize();
        return false;
    }

    return true;
}

IUIAutomationElement*
UiaClient::element_from_window(HWND hwnd) const {
    if (!automation_ || !hwnd) {
        return nullptr;
    }

    IUIAutomationElement* element = nullptr;

    HRESULT hr = automation_->ElementFromHandle(
        hwnd,
        &element
    );

    if (FAILED(hr)) {
        return nullptr;
    }

    return element;
}

}