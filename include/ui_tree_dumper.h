#pragma once

#include <UIAutomation.h>
#include <string>
#include <fstream>

namespace continuum {

class UiTreeDumper {
public:
    static void dump(
        IUIAutomationElement* root,
        const std::wstring& output_path,
        int max_depth = 6
    );

    static bool extract_conversation(
        IUIAutomationElement* root,
        const std::wstring& output_path
    );

    static std::wstring extract_title(
        IUIAutomationElement* root
    );

private:
    static void dump_element(
        IUIAutomationElement* element,
        std::ofstream& output,
        int depth,
        int max_depth
    );
};

}
