#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>

#include "../include/claude_windows_finder.h"
#include "../include/uia_client.h"
#include "../include/ui_tree_dumper.h"
#include "../include/conversation_parser.h"
#include "../include/context_compiler.h"
#include "../include/evidence_repository.h"
#include "../include/ollama_client.h"

#include <fstream>

#include "../include/nlohmann/json.hpp"

int main(int argc, char* argv[]) {

    if (argc > 1 && std::string(argv[1]) == "--compile-local") {
        const std::string model =
            argc > 2 ? argv[2] : "continuum-context";
        const std::filesystem::path prompt_path =
            "output/context/compilation_prompt.md";
        const std::filesystem::path temporary_package =
            "output/context/context_package.generated.json";
        const std::filesystem::path package_path =
            "output/context/context_package.json";

        if (!continuum::ContextCompiler::build_prompt(
                "output/parsed_conversation.json",
                prompt_path)) {
            std::cerr << "Failed to generate compilation prompt.\n";
            return 1;
        }

        std::ifstream input(prompt_path, std::ios::binary);
        std::string prompt{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };

        if (!input && prompt.empty()) {
            std::cerr << "Failed to read compilation prompt.\n";
            return 1;
        }

        // Keep the full artifact manifest on disk for exact local injection,
        // but do not spend model context reproducing source code. The model
        // receives only artifact metadata and the already-redacted
        // conversation; write_package() restores the exact manifest.
        const std::string artifact_marker =
            "DETECTED TECHNICAL ARTIFACTS (PRESERVE IN technical_artifacts):";
        const auto marker_position = prompt.find(artifact_marker);
        if (marker_position != std::string::npos) {
            try {
                auto artifact_metadata = nlohmann::json::parse(
                    prompt.substr(marker_position + artifact_marker.size())
                );
                if (artifact_metadata.is_array()) {
                    for (auto& artifact : artifact_metadata) {
                        if (artifact.is_object()) {
                            artifact.erase("content");
                            artifact.erase("normalized_content");
                        }
                    }
                    prompt = prompt.substr(0, marker_position)
                        + artifact_marker + "\n"
                        + artifact_metadata.dump(2) + "\n";
                }
            }
            catch (const std::exception& exception) {
                std::cerr << "Failed to prepare model prompt: "
                          << exception.what() << '\n';
                return 1;
            }
        }

        continuum::OllamaClient ollama;
        std::string response;
        std::string error;

        if (!ollama.generate_json(model, prompt, response, error)) {
            std::cerr << "Local context generation FAILED: " << error << '\n';
            return 1;
        }

        if (!continuum::ContextCompiler::write_package(
                temporary_package,
                response,
                error)) {
            std::cerr << "Ollama returned an invalid context package: "
                      << error << '\n';
            std::filesystem::remove(temporary_package);
            return 1;
        }

        if (!continuum::ContextCompiler::validate_package(
                temporary_package,
                error)) {
            std::cerr << "Generated context package FAILED validation: "
                      << error << '\n';
            std::filesystem::remove(temporary_package);
            return 1;
        }

        std::filesystem::copy_file(
            temporary_package,
            package_path,
            std::filesystem::copy_options::overwrite_existing
        );
        std::filesystem::remove(temporary_package);

        std::cout << "Local context package generated successfully.\n"
                  << "Model: " << model << '\n'
                  << "Output: output/context/context_package.json\n";
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--compile-project-local") {
        if (argc < 3) {
            std::cerr << "Usage: --compile-project-local <project_id> "
                         "[model] [task]\n";
            return 1;
        }
        const std::string project_id = argv[2];
        const std::string model = argc > 3 ? argv[3] : "qwen2.5-coder:3b";
        const std::string task = argc > 4 ? argv[4] : "Continue the project.";
        const std::filesystem::path prompt_dir =
            "output/context/project-compilation";
        const std::filesystem::path prompt_path =
            prompt_dir / "compilation_prompt.md";
        const std::filesystem::path temporary_package =
            prompt_dir / "context_package.generated.json";
        const std::filesystem::path package_path =
            "output/context/context_package.json";

        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)
            || !repository.build_project_memory_prompt(
                project_id, task, prompt_path, error)) {
            std::cerr << "Project compilation prompt FAILED: " << error << '\n';
            return 1;
        }

        std::ifstream input(prompt_path, std::ios::binary);
        std::string prompt{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        if (!input && prompt.empty()) {
            std::cerr << "Failed to read project compilation prompt.\n";
            return 1;
        }

        const std::string artifact_marker =
            "DETECTED TECHNICAL ARTIFACTS (PRESERVE IN technical_artifacts):";
        const auto marker_position = prompt.find(artifact_marker);
        if (marker_position != std::string::npos) {
            try {
                auto artifact_metadata = nlohmann::json::parse(
                    prompt.substr(marker_position + artifact_marker.size())
                );
                if (artifact_metadata.is_array()) {
                    for (auto& artifact : artifact_metadata) {
                        if (artifact.is_object()) {
                            artifact.erase("content");
                            artifact.erase("normalized_content");
                        }
                    }
                    prompt = prompt.substr(0, marker_position)
                        + artifact_marker + "\n"
                        + artifact_metadata.dump(2) + "\n";
                }
            }
            catch (const std::exception& exception) {
                std::cerr << "Failed to prepare project model prompt: "
                          << exception.what() << '\n';
                return 1;
            }
        }

        continuum::OllamaClient ollama;
        std::string response;
        if (!ollama.generate_json(model, prompt, response, error)) {
            std::cerr << "Project context generation FAILED: " << error << '\n';
            return 1;
        }
        if (!continuum::ContextCompiler::write_package(
                temporary_package, response, error)
            || !continuum::ContextCompiler::validate_package(
                temporary_package, error)) {
            std::cerr << "Generated project context package FAILED validation: "
                      << error << '\n';
            std::filesystem::remove(temporary_package);
            return 1;
        }
        std::filesystem::copy_file(
            temporary_package,
            package_path,
            std::filesystem::copy_options::overwrite_existing
        );
        std::filesystem::remove(temporary_package);
        std::cout << "Project context package generated successfully.\n"
                  << "Project: " << project_id << "\n"
                  << "Model: " << model << '\n'
                  << "Output: output/context/context_package.json\n";
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--import") {
        const std::filesystem::path parsed_path =
            argc > 2 ? argv[2] : "output/parsed_conversation.json";
        const std::string project_id =
            argc > 3 ? argv[3] : "default";

        continuum::EvidenceRepository repository(
            "output/continuum.sqlite"
        );
        std::string error;
        bool duplicate = false;

        if (!repository.import_parsed_conversation(
                parsed_path,
                project_id,
                duplicate,
                error)) {
            std::cerr << "Evidence import FAILED: " << error << '\n';
            return 1;
        }

        if (duplicate) {
            std::cout << "Evidence import skipped: identical conversation already exists.\n";
        } else {
            std::cout << "Evidence import completed.\n";
        }

        continuum::EvidenceCounts counts;
        if (!repository.counts(counts, error)) {
            std::cerr << "Failed to read evidence counts: " << error << '\n';
            return 1;
        }

        std::cout << "Database: output/continuum.sqlite\n"
                  << "Conversations: " << counts.conversations << '\n'
                  << "Messages: " << counts.messages << '\n'
                  << "Artifacts: " << counts.artifacts << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--search") {
        if (argc < 3) {
            std::cerr << "Usage: --search <query> [project_id] [limit]\n";
            return 1;
        }

        const std::string project_id = argc > 3 ? argv[3] : "default";
        std::size_t limit = 10;
        if (argc > 4) {
            try {
                limit = std::stoull(argv[4]);
            }
            catch (...) {
                std::cerr << "Search limit must be a positive integer.\n";
                return 1;
            }
        }

        continuum::EvidenceRepository repository(
            "output/continuum.sqlite"
        );
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "Search initialization FAILED: " << error << '\n';
            return 1;
        }

        std::vector<continuum::EvidenceSearchResult> results;
        if (!repository.search_messages(
                project_id, argv[2], limit, results, error)) {
            std::cerr << "Search FAILED: " << error << '\n';
            return 1;
        }

        for (const auto& result : results) {
            std::cout << result.conversation_id << " message "
                      << result.message_ordinal << " [" << result.role
                      << "] " << result.snippet << '\n';
        }
        std::cout << "Results: " << results.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--list-conversations") {
        const std::string project_id = argc > 2 ? argv[2] : "default";
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "List initialization FAILED: " << error << '\n';
            return 1;
        }
        std::vector<continuum::ConversationSummary> conversations;
        if (!repository.list_conversations(project_id, conversations, error)) {
            std::cerr << "List FAILED: " << error << '\n';
            return 1;
        }
        for (const auto& conversation : conversations) {
            std::cout << conversation.id << " | " << conversation.title
                      << " | messages=" << conversation.message_count
                      << " | artifacts=" << conversation.artifact_count
                      << " | source=" << conversation.source_path << '\n';
        }
        std::cout << "Conversations: " << conversations.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--inspect") {
        if (argc < 3) {
            std::cerr << "Usage: --inspect <conversation_id>\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "Inspect initialization FAILED: " << error << '\n';
            return 1;
        }
        continuum::ConversationSummary summary;
        std::vector<continuum::MessageRecord> messages;
        std::vector<continuum::ArtifactSummary> artifacts;
        if (!repository.inspect_conversation(
                argv[2], summary, messages, artifacts, error)) {
            std::cerr << "Inspect FAILED: " << error << '\n';
            return 1;
        }
        std::cout << summary.id << " | " << summary.title
                  << " | project=" << summary.project_id
                  << " | messages=" << summary.message_count
                  << " | artifacts=" << summary.artifact_count << '\n';
        for (const auto& message : messages) {
            std::cout << "message " << message.ordinal << " ["
                      << message.role << "] " << message.content << '\n';
        }
        for (const auto& artifact : artifacts) {
            std::cout << "artifact " << artifact.id << " | language="
                      << artifact.language << " | status=" << artifact.status
                      << " | tested=" << (artifact.tested ? "true" : "false")
                      << " | supersedes=" << artifact.supersedes_id
                      << " | revision_of=" << artifact.revision_of << '\n';
        }
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--backup") {
        if (argc < 3) {
            std::cerr << "Usage: --backup <backup_path>\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)
            || !repository.backup_to(argv[2], error)) {
            std::cerr << "Backup FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Backup created: " << argv[2] << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--restore") {
        if (argc < 3) {
            std::cerr << "Usage: --restore <backup_path>\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.restore_from(argv[2], error)
            || !repository.initialize(error)) {
            std::cerr << "Restore FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Backup restored: " << argv[2] << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--reprocess") {
        if (argc < 3) {
            std::cerr << "Usage: --reprocess <parsed_conversation.json> [project_id]\n";
            return 1;
        }
        const std::string project_id = argc > 3 ? argv[3] : "default";
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        bool duplicate = false;
        if (!repository.import_parsed_conversation(
                argv[2], project_id, duplicate, error)) {
            std::cerr << "Reprocess FAILED: " << error << '\n';
            return 1;
        }
        std::cout << (duplicate ? "Reprocess unchanged: existing evidence retained.\n"
                                : "Reprocess imported as new evidence revision.\n");
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--propose") {
        if (argc < 3) {
            std::cerr << "Usage: --propose <parsed_conversation.json> [project_id]\n";
            return 1;
        }
        const std::string project_id = argc > 3 ? argv[3] : "default";
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        bool duplicate = false;
        if (!repository.import_parsed_conversation(
                argv[2], project_id, duplicate, error)) {
            std::cerr << "Proposal import FAILED: " << error << '\n';
            return 1;
        }

        std::string conversation_id;
        if (!repository.find_conversation_id(
                argv[2], project_id, conversation_id, error)
            || conversation_id.empty()) {
            std::cerr << "Unable to find imported conversation for proposals.\n";
            return 1;
        }

        std::vector<continuum::ContextProposal> proposals;
        if (!continuum::ContextProposalEngine::extract_from_file(
                argv[2], project_id, proposals, error)
            || !repository.store_proposals(conversation_id, proposals, error)) {
            std::cerr << "Proposal extraction FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Proposals stored: " << proposals.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--list-proposals") {
        if (argc < 3) {
            std::cerr << "Usage: --list-proposals <project_id> [status]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "Proposal listing initialization FAILED: " << error << '\n';
            return 1;
        }
        std::vector<continuum::ContextProposal> proposals;
        if (!repository.list_proposals(
                argv[2], argc > 3 ? argv[3] : "", proposals, error)) {
            std::cerr << "Proposal listing FAILED: " << error << '\n';
            return 1;
        }
        for (const auto& proposal : proposals) {
            std::cout << proposal.id << " | " << proposal.kind
                      << " | " << proposal.status << " | confidence="
                      << proposal.confidence << " | message="
                      << proposal.source_message_index << " | "
                      << proposal.statement << '\n';
        }
        std::cout << "Proposals: " << proposals.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--review-proposal") {
        if (argc < 4) {
            std::cerr << "Usage: --review-proposal <proposal_id> "
                         "<accepted|rejected|supersede> [note] [target_id]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        const std::string note = argc > 4 ? argv[4] : "";
        const std::string target_id = argc > 5 ? argv[5] : "";
        if (!repository.review_proposal(
                argv[2], argv[3], note, error, target_id)) {
            std::cerr << "Proposal review FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Proposal reviewed: " << argv[2] << " -> "
                  << argv[3] << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--list-context") {
        if (argc < 3) {
            std::cerr << "Usage: --list-context <project_id> [all]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "Context listing initialization FAILED: " << error << '\n';
            return 1;
        }
        std::vector<continuum::ContextRecord> records;
        if (!repository.list_context_records(
                argv[2], argc > 3 && std::string(argv[3]) == "all",
                records, error)) {
            std::cerr << "Context listing FAILED: " << error << '\n';
            return 1;
        }
        for (const auto& record : records) {
            std::cout << record.id << " | " << record.kind << " | "
                      << record.status << " | message="
                      << record.source_message_index << " | "
                      << record.statement;
            if (!record.supersedes_record_id.empty()) {
                std::cout << " | supersedes=" << record.supersedes_record_id;
            }
            std::cout << '\n';
        }
        std::cout << "Context records: " << records.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--checkpoint") {
        if (argc < 3) {
            std::cerr << "Usage: --checkpoint <project_id> [label]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        continuum::MemoryCheckpoint checkpoint;
        if (!repository.create_checkpoint(
                argv[2], argc > 3 ? argv[3] : "manual checkpoint",
                checkpoint, error)) {
            std::cerr << "Checkpoint FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Checkpoint " << checkpoint.sequence << " created: "
                  << checkpoint.memory_hash << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--list-checkpoints") {
        if (argc < 3) {
            std::cerr << "Usage: --list-checkpoints <project_id>\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)) {
            std::cerr << "Checkpoint listing initialization FAILED: "
                      << error << '\n';
            return 1;
        }
        std::vector<continuum::MemoryCheckpoint> checkpoints;
        if (!repository.list_checkpoints(argv[2], checkpoints, error)) {
            std::cerr << "Checkpoint listing FAILED: " << error << '\n';
            return 1;
        }
        for (const auto& checkpoint : checkpoints) {
            std::cout << checkpoint.sequence << " | " << checkpoint.label
                      << " | hash=" << checkpoint.memory_hash << '\n';
        }
        std::cout << "Checkpoints: " << checkpoints.size() << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--build-project-prompt") {
        if (argc < 3) {
            std::cerr << "Usage: --build-project-prompt <project_id> [task]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)
            || !repository.build_project_memory_prompt(
                argv[2], argc > 3 ? argv[3] : "",
                "output/context/project_memory_prompt.md", error)) {
            std::cerr << "Project prompt FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Project memory prompt generated successfully.\n"
                  << "Output: output/context/project_memory_prompt.md\n";
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--inspect-memory") {
        if (argc < 3) {
            std::cerr << "Usage: --inspect-memory <project_id> [output_path]\n";
            return 1;
        }
        continuum::EvidenceRepository repository("output/continuum.sqlite");
        std::string error;
        if (!repository.initialize(error)
            || !repository.build_memory_inspection_report(
                argv[2],
                argc > 3 ? argv[3]
                         : "output/context/memory_inspection.md",
                error)) {
            std::cerr << "Memory inspection FAILED: " << error << '\n';
            return 1;
        }
        std::cout << "Memory inspection report generated successfully.\n"
                  << "Output: "
                  << (argc > 3 ? argv[3]
                              : "output/context/memory_inspection.md")
                  << '\n';
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--compile-context") {
    const bool success = continuum::ContextCompiler::build_prompt(
        "output/parsed_conversation.json",
        "output/context/compilation_prompt.md"
    );

    if (!success) {
        std::cerr << "Failed to generate context compilation prompt.\n";
        return 1;
    }

    std::cout << "Context compilation prompt generated successfully.\n";
    std::cout << "Output: output/context/compilation_prompt.md\n";

    return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--build-memory-prompt") {
        std::string error;

        if (!continuum::ContextCompiler::build_memory_prompt(
                "output/context/context_package.json",
                "output/context/memory_update_prompt.md",
                error)) {
            std::cerr << "Failed to build memory update prompt: "
                      << error << '\n';
            return 1;
        }

        std::cout << "Memory update prompt generated successfully.\n";
        return 0;
    }

    if (argc > 1 && std::string(argv[1]) == "--validate-context") {
    std::string error;

    const bool valid = continuum::ContextCompiler::validate_package(
        "output/context/context_package.json",
        error
    );

    if (!valid) {
        std::cerr << "Context package validation FAILED.\n";
        std::cerr << "Reason: " << error << '\n';
        return 1;
    }

    std::cout << "Context package validation PASSED.\n";
    std::cout << "File: output/context/context_package.json\n";

    return 0;
    }

    std::cout << "Continuum Claude Extraction POC\n\n";

    const auto window = continuum::ClaudeWindowFinder::find();

    if (!window.hwnd) {
        std::cout << "Claude window found: NO\n";
        return 1;
    }

    std::wcout << L"Claude window found: YES\n";
    std::wcout << L"HWND:       " << window.hwnd << L'\n';
    std::wcout << L"Title:      " << window.title << L'\n';
    std::wcout << L"ClassName:  " << window.class_name << L'\n';
    std::wcout << L"PID:        " << window.process_id << L'\n';

    continuum::UiaClient uia;

    if (!uia.initialize()) {
        std::cout << "UI Automation initialized: NO\n";
        return 1;
    }

    std::cout << "UI Automation initialized: YES\n";

    auto* root = uia.element_from_window(window.hwnd);

    if (!root) {
        std::cout << "Root element acquired: NO\n";
        return 1;
    }

    std::cout << "Root element acquired: YES\n";

    continuum::UiTreeDumper::dump(
        root,
        L"output/claude_tree.txt",
        12
    );

    bool extracted = false;
    constexpr int max_attempts = 6;

    for (int attempt = 1; attempt <= max_attempts && !extracted; ++attempt) {
        if (!root) {
            root = uia.element_from_window(window.hwnd);
        }

        if (!root) {
            std::cout << "Root element unavailable on extraction attempt "
                      << attempt << ".\n";
        } else {
        extracted = continuum::UiTreeDumper::extract_conversation(
            root,
            L"output/raw_conversation.txt"
        );
        }

        if (!extracted && attempt < max_attempts) {
            if (root) {
                root->Release();
                root = nullptr;
            }
            std::cout << "Conversation not ready; retrying extraction ("
                      << attempt + 1 << "/" << max_attempts << ")...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (!extracted) {
        std::cerr << "Conversation extraction failed; refusing to parse stale output.\n";
        root->Release();
        return 1;
    }

   auto conversation =
    continuum::ConversationParser::parse_file(
        "output/raw_conversation.txt"
    );

    const std::wstring title =
        continuum::UiTreeDumper::extract_title(root);

    if (!title.empty()) {
        conversation.title = std::string(title.begin(), title.end());
    } else {
        conversation.title = "Untitled conversation";
    }

    if (!continuum::ConversationParser::write_json(
        conversation,
        "output/parsed_conversation.json")) {

        std::cout << "Failed to write parsed conversation.\n";
        } else {

        std::cout
            << "Parsed conversation written to: "
            << "output/parsed_conversation.json\n";

        std::cout
            << "Messages detected: "
            << conversation.messages.size()
            << '\n';
    }

    root->Release();

    return 0;
}
