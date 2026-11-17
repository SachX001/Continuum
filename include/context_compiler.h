#pragma once

#include <string>
#include <filesystem>

namespace continuum {

class ContextCompiler {
public:
    static bool build_prompt(
        const std::filesystem::path& conversation_path,
        const std::filesystem::path& prompt_path
    );

    static std::string extract_artifacts_json(
        const std::string& conversation_json
    );

    static std::string normalize_code_for_display(
        const std::string& content
    );

    static bool build_memory_prompt(
        const std::filesystem::path& context_package_path,
        const std::filesystem::path& prompt_path,
        std::string& error
    );

    static bool validate_package(
        const std::filesystem::path& package_path,
        std::string& error
    );

    static bool write_package(
        const std::filesystem::path& package_path,
        const std::string& package_json,
        std::string& error
    );
};

}
