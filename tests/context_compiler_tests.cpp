#include "../include/context_compiler.h"
#include "../include/conversation_parser.h"
#include "../include/evidence_repository.h"
#include "../include/nlohmann/json.hpp"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using json = nlohmann::json;

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }

    return true;
}

bool write_json(const std::filesystem::path& path, const json& value) {
    std::ofstream output(path);

    if (!output) {
        return false;
    }

    output << value.dump(2) << '\n';
    return output.good();
}

bool write_text(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output << value;
    return output.good();
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

std::size_t count_occurrences(
    const std::string& text,
    const std::string& needle
) {
    std::size_t count = 0;
    std::size_t position = 0;

    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }

    return count;
}

json valid_package() {
    return {
        {"schema_version", "1.1"},
        {"project", {
            {"name", "Test project"},
            {"purpose", "Test context transfer"}
        }},
        {"current_objective", "Verify code preservation"},
        {"current_state", json::array()},
        {"completed_work", json::array()},
        {"decisions", json::array()},
        {"constraints", json::array()},
        {"unresolved_issues", json::array()},
        {"next_steps", json::array()},
        {"assumptions", json::array()},
        {"technical_artifacts", json::array({
            {
                {"id", "artifact-1"},
                {"type", "code_block"},
                {"source_role", "assistant"},
                {"language", "python"},
                {"source_message_index", 1},
                {"status", "proposed"},
                {"tested", false},
                {"content", "print('preserved')"}
            }
        })},
        {"source_metadata", {
            {"source_type", "conversation_json"},
            {"message_count", 1}
        }}
    };
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path()
        / "continuum-context-compiler-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    bool passed = true;
    std::string error;

    const auto no_code_input = root / "no-code.json";
    const auto no_code_prompt = root / "no-code.md";
    passed &= write_json(no_code_input, {
        {"title", "No code"},
        {"messages", json::array({
            {{"role", "user"}, {"content", "Explain this concept."}},
            {{"role", "assistant"}, {"content", "Here is a concise explanation."}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(no_code_input, no_code_prompt),
        "conversation without code compiles"
    );
    const std::string no_code_text = read_text(no_code_prompt);
    passed &= check(
        no_code_text.find("DETECTED TECHNICAL ARTIFACTS") != std::string::npos
            && no_code_text.find("source-message-") == std::string::npos,
        "conversation without code produces an empty artifact list"
    );

    const auto parser_regression_raw = root / "python-then-cpp-raw.txt";
    const std::string parser_regression_text =
        "You said: solve this problem\n"
        "solve this problem\n"
        "Show message actions for You said: solve this problem\n"
        "Claude responded: Python answer\n"
        "Python answer\n"
        "Key observations\n"
        "Copy to clipboard\n"
        "python\n"
        "class Solution:\n"
        "    def solve(self):\n"
        "        return 1\n"
        "You said: can you write the solution in C++?\n"
        "can you write the solution in C++?\n"
        "Claude responded: Notes\n"
        "Deciding how to present the C++ solution.\n"
        "Copy to clipboard\n"
        "cpp\n"
        "#include <vector>\n"
        "class Solution {\n"
        "public:\n"
        "    int solve() { return 2; }\n"
        "};\n"
        "Notes\n"
        "The C++ correction supersedes the Python version.\n"
        "Copy\n"
        "Send message\n";
    passed &= check(
        write_text(parser_regression_raw, parser_regression_text),
        "parser regression fixture is written"
    );
    const auto parsed_regression =
        continuum::ConversationParser::parse_file(parser_regression_raw.string());
    passed &= check(
        parsed_regression.messages.size() == 4
            && parsed_regression.messages[1].content.find("class Solution")
                != std::string::npos
            && parsed_regression.messages[3].content.find("#include <vector>")
                != std::string::npos
            && parsed_regression.messages[3].content.find("int solve()")
                != std::string::npos,
        "parser preserves Python and later C++ correction messages"
    );
    const auto parsed_regression_json = root / "python-then-cpp.json";
    const auto parsed_regression_prompt = root / "python-then-cpp.md";
    passed &= check(
        continuum::ConversationParser::write_json(
            parsed_regression,
            parsed_regression_json.string()
        ) && continuum::ContextCompiler::build_prompt(
            parsed_regression_json,
            parsed_regression_prompt
        ),
        "parser regression reaches context compilation"
    );
    const std::string parser_regression_prompt_text =
        read_text(parsed_regression_prompt);
    const std::string parser_marker =
        "DETECTED TECHNICAL ARTIFACTS (PRESERVE IN technical_artifacts):";
    const auto parser_marker_position =
        parser_regression_prompt_text.find(parser_marker);
    if (parser_marker_position != std::string::npos) {
        const json parser_artifacts = json::parse(
            parser_regression_prompt_text.substr(
                parser_marker_position + parser_marker.size()
            )
        );
        passed &= check(
            parser_artifacts.size() == 2
                && parser_artifacts[0]["language"] == "python"
                && parser_artifacts[1]["language"] == "cpp"
                && parser_artifacts[1]["content"].get<std::string>().find(
                    "#include <vector>"
                ) != std::string::npos,
            "compilation manifest preserves both corrected implementations"
        );
    } else {
        passed &= check(false, "parser regression compilation manifest exists");
    }

    const std::string fenced_code =
        "from typing import List\n\n"
        "def solve(values: List[int]) -> int:\n"
        "    return sum(values)";
    const std::string flattened_code =
        "class Example:\n"
        "    def run(self):\n"
        "        return 42";
    const auto code_input = root / "code.json";
    const auto code_prompt = root / "code.md";
    passed &= write_json(code_input, {
        {"title", "Code"},
        {"messages", json::array({
            {{"role", "user"}, {"content", "Implement this."}},
            {{"role", "assistant"}, {"content",
                "```python\n" + fenced_code + "\n```"}},
            {{"role", "assistant"}, {"content",
                "Java\nCopy to clipboard\n" + flattened_code
                + "\nComplexity\nO(1)"}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(code_input, code_prompt),
        "conversation with code compiles"
    );
    const std::string code_prompt_text = read_text(code_prompt);
    passed &= check(
        code_prompt_text.find("from typing") != std::string::npos
            && code_prompt_text.find("def solve") != std::string::npos
            && code_prompt_text.find("class Example") != std::string::npos
            && code_prompt_text.find("return 42") != std::string::npos,
        "original fenced and flattened code are preserved"
    );
    passed &= check(
        code_prompt_text.find("[technical artifact preserved separately]")
            != std::string::npos,
        "general conversation replaces duplicate code with a reference"
    );

    const std::string realistic_flattened =
        "cpp\n"
        "#include <iostream>\n"
        "int main() { return 1; }\n"
        "Output:\n"
        "1\n"
        "2. Overloading ->\n"
        "cpp\n"
        "class SmartPtr { public: int* operator->(); };\n"
        "The rule: operator-> returns a pointer.\n"
        "3. Overloading =\n"
        "cpp\n"
        "Buffer& operator=(const Buffer& other) { return *this; }\n"
        "Key points:\n"
        "Assignment must return a reference.";
    const auto realistic_input = root / "realistic-flattened.json";
    const auto realistic_prompt = root / "realistic-flattened.md";
    passed &= write_json(realistic_input, {
        {"title", "Multiple flattened C++ examples"},
        {"messages", json::array({
            {{"role", "assistant"}, {"content", realistic_flattened}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(
            realistic_input,
            realistic_prompt
        ),
        "multiple flattened code blocks compile"
    );
    const std::string realistic_prompt_text = read_text(realistic_prompt);
    passed &= check(
        count_occurrences(
            realistic_prompt_text,
            "\"id\": \"source-message-1-code-"
        ) == 3
            && realistic_prompt_text.find("int main() { return 1; }")
                != std::string::npos
            && realistic_prompt_text.find("operator->()")
                != std::string::npos
            && realistic_prompt_text.find("operator=(const Buffer& other)")
                != std::string::npos,
        "multiple flattened implementations remain separate artifacts"
    );

    // A real transfer conversation can contain the previous compiler's JSON
    // response inside an assistant message. Ensure that nested artifacts are
    // recovered individually rather than becoming one merged code blob.
    const std::string embedded_v1 =
        "class Parser { public: std::string parse(std::string s) { return s; } };";
    const std::string embedded_v2 =
        "class Parser { public: std::string parse(std::string s) { return trim(s); } };";
    const json embedded_package = {
        {"technical_artifacts", json::array({
            {
                {"id", "parser-v1"},
                {"type", "code_block"},
                {"source_role", "assistant"},
                {"language", "cpp"},
                {"source_message_index", 2},
                {"status", "superseded"},
                {"tested", true},
                {"content", embedded_v1}
            },
            {
                {"id", "parser-v2"},
                {"type", "code_block"},
                {"source_role", "assistant"},
                {"language", "cpp"},
                {"source_message_index", 4},
                {"status", "verified"},
                {"tested", true},
                {"supersedes_id", "parser-v1"},
                {"revision_of", "parser-v1"},
                {"content", embedded_v2}
            }
        })}
    };
    const auto embedded_input = root / "embedded-package-conversation.json";
    const auto embedded_prompt = root / "embedded-package-compilation.md";
    passed &= write_json(embedded_input, {
        {"title", "Transferred parser package"},
        {"messages", json::array({
            {{"role", "user"}, {"content", "Continue debugging this parser."}},
            {{"role", "assistant"}, {"content",
                "Here is the prior context package:\n"
                + embedded_package.dump(2)}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(
            embedded_input,
            embedded_prompt
        ),
        "embedded context package compiles"
    );
    const std::string embedded_prompt_text = read_text(embedded_prompt);
    passed &= check(
        count_occurrences(embedded_prompt_text, "\"id\": \"parser-v") == 2
            && embedded_prompt_text.find(embedded_v1) != std::string::npos
            && embedded_prompt_text.find(embedded_v2) != std::string::npos
            && embedded_prompt_text.find("\"status\": \"superseded\"")
                != std::string::npos
            && embedded_prompt_text.find("\"supersedes_id\": \"parser-v1\"")
                != std::string::npos
            && embedded_prompt_text.find("\"revision_of\": \"parser-v1\"")
                != std::string::npos,
        "embedded artifacts preserve exact code and revision provenance"
    );

    const auto escaped_input = root / "escaped-code.json";
    const auto escaped_prompt = root / "escaped-code.md";
    const std::string escaped_code =
        "```cpp\n"
        "#include \\<iostream>\n"
        "class Foo { int\\* value; \\~Foo() {} };\n"
        "```";
    passed &= write_json(escaped_input, {
        {"messages", json::array({
            {{"role", "assistant"}, {"content", escaped_code}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(
            escaped_input,
            escaped_prompt
        ),
        "escaped code prompt compiles"
    );
    const std::string escaped_prompt_text = read_text(escaped_prompt);
    passed &= check(
        escaped_prompt_text.find("#include <iostream>") != std::string::npos
            && escaped_prompt_text.find("int* value") != std::string::npos
            && escaped_prompt_text.find("~Foo") != std::string::npos
            && escaped_prompt_text.find("\"normalized_content\"")
                != std::string::npos,
        "escaped source remains available with a readable normalized artifact"
    );

    const std::string parser_v1 =
        "std::string parse_message(const std::string& input) {\n"
        "    return trim(input);\n"
        "}";
    const std::string parser_v2 =
        "std::string parse_message(const std::string& input) {\n"
        "    auto normalized = normalize_markers(input);\n"
        "    return trim_preserving_code(normalized);\n"
        "}";
    const auto revision_input = root / "revision-conversation.json";
    const auto revision_prompt = root / "revision-compilation.md";
    passed &= write_json(revision_input, {
        {"title", "Parser debugging and correction"},
        {"messages", json::array({
            {{"role", "user"}, {"content",
                "Implement the conversation parser."}},
            {{"role", "assistant"}, {"content",
                "The first implementation is below.\n```cpp\n"
                + parser_v1 + "\n```"}},
            {{"role", "user"}, {"content",
                "Debugging: duplicate markers cause code blocks to be lost."}},
            {{"role", "assistant"}, {"content",
                "Correction: this implementation preserves code blocks.\n"
                "```cpp\n" + parser_v2 + "\n```"}},
            {{"role", "user"}, {"content",
                "The correction explicitly supersedes the first implementation."}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(
            revision_input,
            revision_prompt
        ),
        "multi-revision conversation compiles"
    );
    const std::string revision_prompt_text = read_text(revision_prompt);
    passed &= check(
        revision_prompt_text.find("trim(input)") != std::string::npos
            && revision_prompt_text.find("trim_preserving_code") != std::string::npos
            && revision_prompt_text.find("source-message-2-code-1")
                != std::string::npos
            && revision_prompt_text.find("source-message-4-code-1")
                != std::string::npos
            && revision_prompt_text.find("supersedes_id")
                != std::string::npos
            && revision_prompt_text.find("revision_of")
                != std::string::npos,
        "both original and corrected implementations retain exact source evidence"
    );

    json revision_package = valid_package();
    revision_package["project"]["name"] = "Parser debugging and correction";
    revision_package["current_objective"] =
        "Continue with the corrected parser implementation.";
    revision_package["current_state"] = json::array({
        "The first parser implementation lost code blocks when duplicate markers appeared.",
        "The current implementation normalizes markers and preserves code blocks.",
        "The current implementation is parser-v2."
    });
    revision_package["decisions"] = json::array({
        "parser-v2 supersedes parser-v1 because it preserves code blocks after the duplicate-marker bug was identified."
    });
    revision_package["source_metadata"]["message_count"] = 5;
    revision_package["technical_artifacts"] = json::array({
        {
            {"id", "parser-v1"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 2},
            {"status", "superseded"},
            {"tested", true},
            {"content", parser_v1}
        },
        {
            {"id", "parser-v2"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 4},
            {"status", "verified"},
            {"tested", true},
            {"supersedes_id", "parser-v1"},
            {"revision_of", "parser-v1"},
            {"content", parser_v2}
        }
    });

    const auto revision_package_path = root / "revision-package.json";
    const auto revision_memory_prompt = root / "revision-memory.md";
    passed &= write_json(revision_package_path, revision_package);
    error.clear();
    passed &= check(
        continuum::ContextCompiler::validate_package(
            revision_package_path,
            error
        ),
        "revision package with supersession links passes validation"
    );
    passed &= check(
        continuum::ContextCompiler::build_memory_prompt(
            revision_package_path,
            revision_memory_prompt,
            error
        ),
        "revision package produces a transfer prompt"
    );
    const std::string revision_memory_text =
        read_text(revision_memory_prompt);
    passed &= check(
        revision_memory_text.find("trim(input)") != std::string::npos
            && revision_memory_text.find("trim_preserving_code")
                != std::string::npos
            && revision_memory_text.find("supersedes_id") != std::string::npos
            && revision_memory_text.find("parser-v2") != std::string::npos
            && revision_memory_text.find("parser-v1") != std::string::npos
            && revision_memory_text.find("duplicate-marker bug")
                != std::string::npos,
        "fresh-session transfer prompt contains current code, prior code, provenance, and rationale"
    );

    const auto revision_output_dir =
    std::filesystem::current_path()
    / "output"
    / "context"
    / "revision-test";

    std::filesystem::create_directories(revision_output_dir);

    const auto exported_package =
        revision_output_dir / "context_package.json";

    const auto exported_prompt =
        revision_output_dir / "memory_update_prompt.md";

    passed &= check(
        write_json(exported_package, revision_package),
        "export revision context package"
    );

    error.clear();

    passed &= check(
        continuum::ContextCompiler::build_memory_prompt(
            exported_package,
            exported_prompt,
            error
        ),
        "generate exported revision transfer prompt"
    );

    const auto package_path = root / "package.json";
    const auto memory_prompt_path = root / "memory.md";
    passed &= write_json(package_path, valid_package());

    passed &= check(
        continuum::ContextCompiler::validate_package(package_path, error),
        "valid technical artifact package passes validation"
    );
    passed &= check(
        continuum::ContextCompiler::build_memory_prompt(
            package_path,
            memory_prompt_path,
            error
        ),
        "memory prompt is generated from the artifact package"
    );
    const std::string memory_prompt_text = read_text(memory_prompt_path);
    passed &= check(
        memory_prompt_text.find("print('preserved')") != std::string::npos,
        "memory prompt contains recoverable original code"
    );

    {
    const auto repository_path = root / "continuum.sqlite";
    continuum::EvidenceRepository repository(repository_path);
    error.clear();
    passed &= check(
        repository.initialize(error),
        "evidence repository initializes"
    );

    bool duplicate = false;
    const json code_conversation = json::parse(read_text(code_input));
    const std::size_t expected_repository_artifacts = json::parse(
        continuum::ContextCompiler::extract_artifacts_json(
            code_conversation.dump()
        )
    ).size();
    passed &= check(
        repository.import_parsed_conversation(
            code_input,
            "operator-overloading",
            duplicate,
            error
        ) && !duplicate,
        "first parsed conversation import commits"
    );

    continuum::EvidenceCounts repository_counts;
    passed &= check(
        repository.counts(repository_counts, error)
            && repository_counts.conversations == 1
            && repository_counts.messages == 3,
        "repository stores conversation and ordered messages"
    );

    passed &= check(
        repository_counts.artifacts == expected_repository_artifacts
            && expected_repository_artifacts > 0,
        "repository stores fenced and flattened artifacts"
    );

    json second_conversation = code_conversation;
    second_conversation["title"] = "Second operator-overloading conversation";
    second_conversation["messages"][0]["content"] =
        "Implement the examples with additional ownership notes.";
    const auto second_input = root / "second-code.json";
    passed &= write_json(second_input, second_conversation);
    duplicate = false;
    passed &= check(
        repository.import_parsed_conversation(
            second_input,
            "operator-overloading",
            duplicate,
            error
        ) && !duplicate,
        "second conversation with repeated local artifact ids imports"
    );

    continuum::EvidenceCounts after_second;
    passed &= check(
        repository.counts(after_second, error)
            && after_second.conversations == 2
            && after_second.artifacts == expected_repository_artifacts * 2,
        "artifact ids are scoped to their conversation"
    );

    std::vector<continuum::EvidenceSearchResult> search_results;
    passed &= check(
        repository.search_messages(
            "operator-overloading",
            "ownership notes",
            10,
            search_results,
            error
        )
            && search_results.size() == 1
            && search_results[0].message_ordinal == 1,
        "FTS retrieval finds the matching message in the project"
    );
    search_results.clear();
    passed &= check(
        repository.search_messages(
            "missing-project",
            "ownership notes",
            10,
            search_results,
            error
        ) && search_results.empty(),
        "FTS retrieval applies the project filter"
    );

    std::vector<continuum::ConversationSummary> conversation_list;
    passed &= check(
        repository.list_conversations(
            "operator-overloading", conversation_list, error
        )
            && conversation_list.size() == 2,
        "conversation listing exposes imported records"
    );

    if (!conversation_list.empty()) {
        continuum::ConversationSummary inspected;
        std::vector<continuum::MessageRecord> inspected_messages;
        std::vector<continuum::ArtifactSummary> inspected_artifacts;
        passed &= check(
            repository.inspect_conversation(
                conversation_list.front().id,
                inspected,
                inspected_messages,
                inspected_artifacts,
                error
            )
                && inspected.id == conversation_list.front().id
                && inspected_messages.size() == 3,
            "conversation inspection returns ordered messages and artifacts"
        );
    }

    const auto backup_path = root / "continuum-backup.sqlite";
    passed &= check(
        repository.backup_to(backup_path, error)
            && std::filesystem::exists(backup_path),
        "repository backup is created"
    );
    {
        continuum::EvidenceRepository restored(root / "restored.sqlite");
        continuum::EvidenceCounts restored_counts;
        passed &= check(
            restored.restore_from(backup_path, error)
                && restored.initialize(error)
                && restored.counts(restored_counts, error)
                && restored_counts.conversations == after_second.conversations
                && restored_counts.messages == after_second.messages
                && restored_counts.artifacts == after_second.artifacts,
            "backup restores all evidence counts"
        );
    }

    duplicate = false;
    passed &= check(
        repository.import_parsed_conversation(
            code_input,
            "operator-overloading",
            duplicate,
            error
        ) && duplicate,
        "identical conversation import is idempotent"
    );

    continuum::EvidenceCounts after_duplicate;
    passed &= check(
        repository.counts(after_duplicate, error)
            && after_duplicate.conversations == after_second.conversations
            && after_duplicate.messages == after_second.messages
            && after_duplicate.artifacts == after_second.artifacts,
        "duplicate import does not create records"
    );

    const auto invalid_conversation = root / "invalid-conversation.json";
    passed &= write_json(invalid_conversation, {
        {"messages", json::array({
            {{"role", "assistant"}}
        })}
    });
    duplicate = false;
    error.clear();
    passed &= check(
        !repository.import_parsed_conversation(
            invalid_conversation,
            "operator-overloading",
            duplicate,
            error
        ) && error.find("has no string content") != std::string::npos,
        "invalid message rejects the import"
    );

    continuum::EvidenceCounts after_failure;
    passed &= check(
        repository.counts(after_failure, error)
            && after_failure.conversations == after_second.conversations
            && after_failure.messages == after_second.messages
            && after_failure.artifacts == after_second.artifacts,
        "failed import leaves no partial database records"
    );

    const auto proposal_input = root / "proposal-conversation.json";
    passed &= write_json(proposal_input, {
        {"messages", json::array({
            { {"role", "user"}, {"content",
                "We decided to use SQLite for local storage.\n"
                "The system must preserve source provenance."} },
            { {"role", "assistant"}, {"content",
                "The parser failed because of a duplicate marker bug.\n"
                "Next step: implement a review workflow."} },
            { {"role", "assistant"}, {"content",
                "```cpp\n// this must not become a proposal\n```"} }
        })}
    });
    duplicate = false;
    passed &= check(
        repository.import_parsed_conversation(
            proposal_input,
            "proposal-project",
            duplicate,
            error
        ) && !duplicate,
        "proposal fixture imports"
    );
    std::vector<continuum::ContextProposal> extracted_proposals;
    passed &= check(
        continuum::ContextProposalEngine::extract_from_file(
            proposal_input,
            "proposal-project",
            extracted_proposals,
            error
        )
            && extracted_proposals.size() == 4,
        "deterministic proposal extraction classifies explicit statements"
    );
    std::string proposal_conversation_id;
    passed &= check(
        repository.find_conversation_id(
            proposal_input,
            "proposal-project",
            proposal_conversation_id,
            error
        ) && !proposal_conversation_id.empty()
            && repository.store_proposals(
                proposal_conversation_id,
                extracted_proposals,
                error),
        "proposals persist with conversation provenance"
    );
    std::vector<continuum::ContextProposal> stored_proposals;
    passed &= check(
        repository.list_proposals(
            "proposal-project", "proposed", stored_proposals, error
        ) && stored_proposals.size() == extracted_proposals.size(),
        "proposals remain reviewable before acceptance"
    );
    auto decision_proposal = std::find_if(
        stored_proposals.begin(), stored_proposals.end(),
        [](const continuum::ContextProposal& proposal) {
            return proposal.kind == "decision";
        }
    );
    if (decision_proposal != stored_proposals.end()) {
        passed &= check(
            repository.review_proposal(
                decision_proposal->id,
                "accepted",
                "Confirmed by user review",
                error
            ),
            "accepted proposal records a review action"
        );
        std::vector<continuum::ContextProposal> accepted_proposals;
        passed &= check(
            repository.list_proposals(
                "proposal-project", "accepted", accepted_proposals, error
            ) && accepted_proposals.size() == 1,
            "accepted proposal is visible in accepted state"
        );

        const continuum::ContextProposal conflicting_proposal{
            "proposal-conflicting-decision",
            "proposal-project",
            "decision",
            "We decided to use PostgreSQL for local storage.",
            "proposed",
            0.8,
            1,
            "user",
            {},
            {}
        };
        passed &= check(
            repository.store_proposals(
                proposal_conversation_id,
                {conflicting_proposal},
                error
            ),
            "conflicting proposal persists for review"
        );
        error.clear();
        passed &= check(
            !repository.review_proposal(
                conflicting_proposal.id,
                "accepted",
                "",
                error
            ) && error.find("Conflicting accepted") != std::string::npos,
            "conflicting proposal cannot be accepted silently"
        );
        passed &= check(
            repository.review_proposal(
                conflicting_proposal.id,
                "supersede",
                "Correction confirmed by user",
                error,
                decision_proposal->id
            ),
            "supersession review accepts explicit correction"
        );
        std::vector<continuum::ContextRecord> context_records;
        passed &= check(
            repository.list_context_records(
                "proposal-project", true, context_records, error
            ) && context_records.size() == 2
                && std::count_if(
                    context_records.begin(), context_records.end(),
                    [](const continuum::ContextRecord& record) {
                        return record.status == "superseded";
                    }
                ) == 1,
            "context history preserves superseded and current records"
        );
        std::vector<continuum::ContextRecord> current_records;
        passed &= check(
            repository.list_context_records(
                "proposal-project", false, current_records, error
            ) && current_records.size() == 1
                && current_records.front().status == "accepted",
            "current memory excludes superseded records"
        );
        continuum::MemoryCheckpoint checkpoint;
        passed &= check(
            repository.create_checkpoint(
                "proposal-project", "after correction", checkpoint, error
            ) && checkpoint.sequence == 1
                && !checkpoint.memory_hash.empty(),
            "memory checkpoint hashes the current view"
        );
        std::vector<continuum::MemoryCheckpoint> checkpoints;
        passed &= check(
            repository.list_checkpoints(
                "proposal-project", checkpoints, error
            ) && checkpoints.size() == 1
                && checkpoints.front().memory_hash == checkpoint.memory_hash,
            "memory checkpoint is inspectable"
        );
        const auto project_prompt = root / "project-memory.md";
        passed &= check(
            repository.build_project_memory_prompt(
                "proposal-project", "Continue the reviewed project.",
                project_prompt, error
            ) && read_text(project_prompt).find("PostgreSQL") != std::string::npos
                && read_text(project_prompt).find("SQLite") == std::string::npos,
            "project prompt uses only current accepted memory"
        );
        passed &= check(
            read_text(project_prompt).find(
                "DETECTED TECHNICAL ARTIFACTS"
            ) != std::string::npos
                && read_text(project_prompt).find(
                    "this must not become a proposal"
                ) != std::string::npos,
            "project prompt includes artifacts linked to accepted memory"
        );
        const auto memory_report = root / "memory-inspection.md";
        passed &= check(
            repository.build_memory_inspection_report(
                "proposal-project", memory_report, error
            ) && read_text(memory_report).find("Current Accepted Memory")
                != std::string::npos
                && read_text(memory_report).find("Historical Superseded Memory")
                != std::string::npos
                && read_text(memory_report).find("after correction")
                != std::string::npos,
            "memory inspection report shows current history and checkpoints"
        );
    }
    }

    const auto evidence_root = root / "artifact-evidence";
    std::filesystem::create_directories(evidence_root);
    const auto evidence_conversation = evidence_root / "conversation.json";
    const auto evidence_prompt = evidence_root / "compilation_prompt.md";
    const std::string evidence_code_1 = "int first() { return 1; }";
    const std::string evidence_code_2 = "int second() { return 2; }";
    const std::string evidence_code_3 = "int third() { return 3; }";
    passed &= write_json(evidence_conversation, {
        {"messages", json::array({
            {{"role", "assistant"}, {"content",
                "```cpp\n" + evidence_code_1 + "\n```\n"
                "```cpp\n" + evidence_code_2 + "\n```\n"
                "```cpp\n" + evidence_code_3 + "\n```"}}
        })}
    });
    passed &= check(
        continuum::ContextCompiler::build_prompt(
            evidence_conversation,
            evidence_prompt
        ),
        "artifact evidence prompt is generated"
    );

    json merged_package = valid_package();
    merged_package["technical_artifacts"] = json::array({
        {
            {"id", "source-message-1-code-1"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 1},
            {"status", "proposed"},
            {"tested", false},
            {"content", evidence_code_1 + "\n" + evidence_code_2
                + "\n" + evidence_code_3}
        }
    });
    const auto merged_package_path = evidence_root / "context_package.json";
    passed &= write_json(merged_package_path, merged_package);
    error.clear();
    passed &= check(
        !continuum::ContextCompiler::validate_package(
            merged_package_path,
            error
        ) && error.find("lost or merged") != std::string::npos,
        "merged artifact package is rejected against source evidence"
    );

    json lossless_package = valid_package();
    lossless_package["technical_artifacts"] = json::array({
        {
            {"id", "source-message-1-code-1"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 1},
            {"status", "proposed"},
            {"tested", false},
            {"content", evidence_code_1}
        },
        {
            {"id", "source-message-1-code-2"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 1},
            {"status", "proposed"},
            {"tested", false},
            {"content", evidence_code_2}
        },
        {
            {"id", "source-message-1-code-3"},
            {"type", "code_block"},
            {"source_role", "assistant"},
            {"language", "cpp"},
            {"source_message_index", 1},
            {"status", "proposed"},
            {"tested", false},
            {"content", evidence_code_3}
        }
    });
    const auto lossless_package_path = evidence_root / "lossless_package.json";
    passed &= write_json(lossless_package_path, lossless_package);
    error.clear();
    passed &= check(
        continuum::ContextCompiler::validate_package(
            lossless_package_path,
            error
        ),
        "lossless artifact package passes source evidence validation"
    );

    json invalid = valid_package();
    invalid["technical_artifacts"][0].erase("content");
    const auto invalid_path = root / "invalid.json";
    passed &= write_json(invalid_path, invalid);
    error.clear();
    passed &= check(
        !continuum::ContextCompiler::validate_package(invalid_path, error),
        "invalid artifact package is rejected"
    );

    std::filesystem::remove_all(root);

    if (!passed) {
        return 1;
    }

    std::cout << "All context compiler regression tests passed.\n";
    return 0;
}
