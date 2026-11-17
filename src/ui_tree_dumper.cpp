#include "../include/ui_tree_dumper.h"

#include <iostream>
#include <sstream>
#include <Windows.h>
#include <filesystem>

namespace continuum {

namespace {

std::wstring indent(int depth) {
    return std::wstring(depth * 2, L' ');
}

std::wstring get_control_type(IUIAutomationElement* element) {
    CONTROLTYPEID control_type = 0;

    if (FAILED(element->get_CurrentControlType(&control_type))) {
        return L"Unknown";
    }

    switch (control_type) {
        case UIA_WindowControlTypeId:
            return L"Window";
        case UIA_ButtonControlTypeId:
            return L"Button";
        case UIA_EditControlTypeId:
            return L"Edit";
        case UIA_TextControlTypeId:
            return L"Text";
        case UIA_DocumentControlTypeId:
            return L"Document";
        case UIA_PaneControlTypeId:
            return L"Pane";
        case UIA_ListControlTypeId:
            return L"List";
        case UIA_ListItemControlTypeId:
            return L"ListItem";
        case UIA_GroupControlTypeId:
            return L"Group";
        case UIA_CustomControlTypeId:
            return L"Custom";
        default:
            return L"Other";
    }
}

std::wstring get_property(
    IUIAutomationElement* element,
    HRESULT (IUIAutomationElement::*getter)(BSTR*)
) {
    BSTR value = nullptr;

    HRESULT hr = (element->*getter)(&value);

    if (FAILED(hr) || !value) {
        return L"";
    }

    std::wstring result(value);
    SysFreeString(value);

    return result;
}

std::string to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 0) {
        return {};
    }

    std::string result(size, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr
    );

    return result;
}

bool has_pattern(
    IUIAutomationElement* element,
    PATTERNID pattern_id
) {
    IUnknown* pattern = nullptr;

    HRESULT hr = element->GetCurrentPattern(
        pattern_id,
        &pattern
    );

    if (SUCCEEDED(hr) && pattern) {
        pattern->Release();
        return true;
    }

    return false;
}

std::wstring get_text_pattern(
    IUIAutomationElement* element
) {
    if (!element) {
        return L"";
    }

    IUnknown* unknown = nullptr;

    HRESULT hr = element->GetCurrentPattern(
        UIA_TextPatternId,
        &unknown
    );

    if (FAILED(hr) || !unknown) {
        return L"";
    }

    IUIAutomationTextPattern* text_pattern = nullptr;

    hr = unknown->QueryInterface(
        IID_PPV_ARGS(&text_pattern)
    );

    unknown->Release();

    if (FAILED(hr) || !text_pattern) {
        return L"";
    }

    IUIAutomationTextRange* document_range = nullptr;

    hr = text_pattern->get_DocumentRange(
        &document_range
    );

    text_pattern->Release();

    if (FAILED(hr) || !document_range) {
        return L"";
    }

    BSTR text = nullptr;

    hr = document_range->GetText(
        -1,
        &text
    );

    document_range->Release();

    if (FAILED(hr) || !text) {
        return L"";
    }

    std::wstring result(text);

    SysFreeString(text);

    return result;
}

struct ConversationCandidate {
    std::wstring text;
    std::wstring name;
    int length = 0;
};

void find_conversation_document(
    IUIAutomationElement* element,
    ConversationCandidate& best
) {
    if (!element) {
        return;
    }

    // Every Document with TextPattern is a candidate.
    if (get_control_type(element) == L"Document") {

        std::wstring text = get_text_pattern(element);

        if (!text.empty()) {

            std::wcout
                << L"Document candidate: "
                << text.size()
                << L" characters\n";

            if (static_cast<int>(text.size()) > best.length) {

                best.text = std::move(text);

                best.name = get_property(
                    element,
                    &IUIAutomationElement::get_CurrentName
                );

                best.length =
                    static_cast<int>(best.text.size());
            }
        }
    }

    IUIAutomation* automation = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation)
    );

    if (FAILED(hr) || !automation) {
        return;
    }

    IUIAutomationTreeWalker* walker = nullptr;

    hr = automation->get_RawViewWalker(&walker);

    if (FAILED(hr) || !walker) {
        automation->Release();
        return;
    }

    IUIAutomationElement* child = nullptr;

    hr = walker->GetFirstChildElement(
        element,
        &child
    );

    while (SUCCEEDED(hr) && child) {

        find_conversation_document(
            child,
            best
        );

        IUIAutomationElement* next = nullptr;

        hr = walker->GetNextSiblingElement(
            child,
            &next
        );

        child->Release();
        child = next;
    }

    walker->Release();
    automation->Release();
}

void find_documents_with_query(
    IUIAutomationElement* root,
    ConversationCandidate& best
) {
    if (!root) {
        return;
    }

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation)
    );

    if (FAILED(hr) || !automation) {
        return;
    }

    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = UIA_DocumentControlTypeId;

    IUIAutomationCondition* condition = nullptr;
    hr = automation->CreatePropertyCondition(
        UIA_ControlTypePropertyId,
        value,
        &condition
    );

    VariantClear(&value);

    if (FAILED(hr) || !condition) {
        automation->Release();
        return;
    }

    IUIAutomationElementArray* documents = nullptr;
    hr = root->FindAll(
        TreeScope_Subtree,
        condition,
        &documents
    );

    condition->Release();

    if (SUCCEEDED(hr) && documents) {
        int length = 0;
        documents->get_Length(&length);

        for (int i = 0; i < length; ++i) {
            IUIAutomationElement* document = nullptr;

            if (SUCCEEDED(documents->GetElement(i, &document)) && document) {
                const std::wstring text = get_text_pattern(document);

                if (!text.empty()
                    && static_cast<int>(text.size()) > best.length) {
                    best.text = text;
                    best.name = get_property(
                        document,
                        &IUIAutomationElement::get_CurrentName
                    );
                    best.length = static_cast<int>(text.size());
                }

                document->Release();
            }
        }

        documents->Release();
    }

    automation->Release();
}

}

void UiTreeDumper::dump(
    IUIAutomationElement* root,
    const std::wstring& output_path,
    int max_depth
) {
    if (!root) {
        std::wcerr << L"Cannot dump UI tree: root is null.\n";
        return;
    }

    std::ofstream output{
        std::filesystem::path(output_path)
    };

    if (!output.is_open()) {
        std::wcerr << L"Failed to open output file: "
                   << output_path << L'\n';
        return;
    }

    dump_element(root, output, 0, max_depth);

    output.close();

    std::wcout << L"UI tree written to: "
               << output_path << L'\n';
}

bool UiTreeDumper::extract_conversation(
    IUIAutomationElement* root,
    const std::wstring& output_path
) {
    if (!root) {
        std::wcerr
            << L"Cannot extract conversation: root is null.\n";
        return false;
    }

    ConversationCandidate best;

    find_conversation_document(
        root,
        best
    );

    // Chromium-backed applications can expose virtualized Document nodes to
    // FindAll even when the raw tree walker does not enumerate them.
    if (best.text.empty()) {
        find_documents_with_query(root, best);
    }

    if (best.text.empty()) {
        std::wcerr
            << L"No conversation RootWebArea found.\n";
        return false;
    }

    std::ofstream output{
        std::filesystem::path(output_path),
        std::ios::binary
    };

    if (!output.is_open()) {
        std::wcerr
            << L"Failed to open conversation output: "
            << output_path
            << L'\n';

        return false;
    }

    output << to_utf8(best.text);

    output.close();

    std::wcout
        << L"Conversation extracted successfully.\n";

    if (!best.name.empty()) {
        std::wcout
            << L"Conversation: "
            << best.name
            << L'\n';
    }

    std::wcout
        << L"Characters extracted: "
        << best.length
        << L'\n';

    std::wcout
        << L"Conversation written to: "
        << output_path
        << L'\n';

    return output.good();
}

std::wstring UiTreeDumper::extract_title(IUIAutomationElement* root) {
    if (!root) {
        return L"";
    }

    std::wstring control_type = get_control_type(root);
    std::wstring automation_id = get_property(
        root,
        &IUIAutomationElement::get_CurrentAutomationId
    );

    if (control_type == L"Document" &&
        automation_id == L"RootWebArea") {

        std::wstring name = get_property(
            root,
            &IUIAutomationElement::get_CurrentName
        );

        const std::wstring suffix = L" - Claude";

        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(),
                         suffix.size(), suffix) == 0) {
            name.erase(name.size() - suffix.size());
        }

        return name;
    }

    IUIAutomation* automation = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation)
    );

    if (FAILED(hr) || !automation) {
        return L"";
    }

    IUIAutomationTreeWalker* walker = nullptr;
    hr = automation->get_RawViewWalker(&walker);

    if (FAILED(hr) || !walker) {
        automation->Release();
        return L"";
    }

    IUIAutomationElement* child = nullptr;
    hr = walker->GetFirstChildElement(root, &child);

    std::wstring title;

    while (SUCCEEDED(hr) && child) {
        title = extract_title(child);

        if (!title.empty()) {
            child->Release();
            break;
        }

        IUIAutomationElement* next = nullptr;
        hr = walker->GetNextSiblingElement(child, &next);

        child->Release();
        child = next;
    }

    if (child) {
        child->Release();
    }

    walker->Release();
    automation->Release();

    return title;
}

void UiTreeDumper::dump_element(
    IUIAutomationElement* element,
    std::ofstream& output,
    int depth,
    int max_depth
) {
    if (!element || depth > max_depth) {
        return;
    }

    std::wstring control_type = get_control_type(element);

    std::wstring name = get_property(
        element,
        &IUIAutomationElement::get_CurrentName
    );

    std::wstring class_name = get_property(
        element,
        &IUIAutomationElement::get_CurrentClassName
    );

    std::wstring automation_id = get_property(
        element,
        &IUIAutomationElement::get_CurrentAutomationId
    );

    output << std::string(depth * 2, ' ')
           << "[" << to_utf8(control_type) << "]";

    if (!name.empty()) {
        output << " Name=\"" << to_utf8(name) << "\"";
    }

    if (!class_name.empty()) {
        output << " Class=\"" << to_utf8(class_name) << "\"";
    }

    if (!automation_id.empty()) {
        output << " AutomationId=\"" << to_utf8(automation_id) << "\"";
    }

    output << '\n';

    if (control_type == L"Document") {
        output << std::string((depth + 1) * 2, ' ')
            << "Patterns:\n";

        output << std::string((depth + 2) * 2, ' ')
            << "TextPattern="
            << (has_pattern(element, UIA_TextPatternId) ? "YES" : "NO")
            << '\n';

        output << std::string((depth + 2) * 2, ' ')
            << "ValuePattern="
            << (has_pattern(element, UIA_ValuePatternId) ? "YES" : "NO")
            << '\n';

        output << std::string((depth + 2) * 2, ' ')
            << "LegacyIAccessible="
            << (has_pattern(element, UIA_LegacyIAccessiblePatternId) ? "YES" : "NO")
            << '\n';

        output << std::string((depth + 2) * 2, ' ')
            << "ScrollPattern="
            << (has_pattern(element, UIA_ScrollPatternId) ? "YES" : "NO")
            << '\n';

        output << std::string((depth + 2) * 2, ' ')
            << "SelectionPattern="
            << (has_pattern(element, UIA_SelectionPatternId) ? "YES" : "NO")
            << '\n';
    }

    std::wstring extracted_text = get_text_pattern(element);

    if (!extracted_text.empty()) {
        output << std::string((depth + 1) * 2, ' ')
            << "ExtractedText:\n";

        output << to_utf8(extracted_text);

        output << "\n";
    }

    if (depth >= max_depth) {
        return;
    }

    IUIAutomation* automation = nullptr;

    HRESULT hr = CoCreateInstance(
        CLSID_CUIAutomation,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&automation)
    );

    if (FAILED(hr) || !automation) {
        return;
    }

    IUIAutomationTreeWalker* walker = nullptr;

    hr = automation->get_RawViewWalker(&walker);

    if (FAILED(hr) || !walker) {
        automation->Release();
        return;
    }

    IUIAutomationElement* child = nullptr;

    hr = walker->GetFirstChildElement(
        element,
        &child
    );

    while (SUCCEEDED(hr) && child) {

        dump_element(
            child,
            output,
            depth + 1,
            max_depth
        );

        IUIAutomationElement* next = nullptr;

        hr = walker->GetNextSiblingElement(
            child,
            &next
        );

        child->Release();
        child = next;
    }

    walker->Release();
    automation->Release();
}

}
