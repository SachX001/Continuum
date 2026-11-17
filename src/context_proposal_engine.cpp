#include "../include/context_proposal_engine.h"
#include "../include/nlohmann/json.hpp"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace continuum {

using json = nlohmann::json;

namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    return value;
}

std::string content_hash(const std::string& value) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

void add_candidate(
    std::vector<ContextProposal>& proposals,
    const std::string& project_id,
    const std::string& kind,
    const std::string& statement,
    double confidence,
    std::size_t message_index,
    const std::string& role
) {
    const std::string normalized = trim(statement);
    if (normalized.size() < 8 || normalized.size() > 320) {
        return;
    }

    const std::string id = "proposal-" + content_hash(
        project_id + "|" + kind + "|"
        + std::to_string(message_index) + "|" + normalized);
    for (const auto& existing : proposals) {
        if (existing.id == id) {
            return;
        }
    }
    proposals.push_back({
        id, project_id, kind, normalized, "proposed", confidence,
        message_index, role, {}, {}
    });
}

}

bool ContextProposalEngine::extract_from_file(
    const std::filesystem::path& parsed_path,
    const std::string& project_id,
    std::vector<ContextProposal>& proposals,
    std::string& error
) {
    proposals.clear();
    std::ifstream input(parsed_path, std::ios::binary);
    if (!input) {
        error = "Unable to open parsed conversation: " + parsed_path.string();
        return false;
    }

    try {
        json conversation;
        input >> conversation;
        if (!conversation.is_object()
            || !conversation.contains("messages")
            || !conversation["messages"].is_array()) {
            error = "Parsed conversation must contain a messages array.";
            return false;
        }

        for (std::size_t index = 0;
             index < conversation["messages"].size(); ++index) {
            const auto& message = conversation["messages"][index];
            if (!message.is_object() || !message["content"].is_string()) {
                continue;
            }

            const std::string role = message.value("role", "unknown");
            std::istringstream lines(message["content"].get<std::string>());
            std::string line;
            bool in_code = false;
            while (std::getline(lines, line)) {
                const std::string candidate = trim(line);
                if (candidate.rfind("```", 0) == 0) {
                    in_code = !in_code;
                    continue;
                }
                if (in_code || candidate.empty()) {
                    continue;
                }

                std::string statement = candidate;
                while (!statement.empty()
                    && (statement.front() == '-'
                        || statement.front() == '*'
                        || statement.front() == '#')) {
                    statement = trim(statement.substr(1));
                }
                const std::string text = lower(statement);

                if (text.find("unresolved") != std::string::npos
                    || text.find("not working") != std::string::npos
                    || text.find("failed") != std::string::npos
                    || text.find("bug") != std::string::npos
                    || text.find("error") != std::string::npos
                    || text.find("todo") != std::string::npos) {
                    add_candidate(proposals, project_id, "unresolved_issue",
                        statement, 0.85, index + 1, role);
                } else if (text.find("we will") != std::string::npos
                    || text.find("decided") != std::string::npos
                    || text.find("chosen") != std::string::npos
                    || text.find("decision") != std::string::npos) {
                    add_candidate(proposals, project_id, "decision",
                        statement, 0.8, index + 1, role);
                } else if (text.find("must ") != std::string::npos
                    || text.find("required") != std::string::npos
                    || text.find("constraint") != std::string::npos
                    || text.find("should not") != std::string::npos) {
                    add_candidate(proposals, project_id, "constraint",
                        statement, 0.75, index + 1, role);
                } else if (text.find("next step") != std::string::npos
                    || text.find("need to ") != std::string::npos
                    || text.find("implement ") != std::string::npos
                    || text.find("add ") == 0
                    || text.find("fix ") == 0
                    || text.find("build ") == 0) {
                    add_candidate(proposals, project_id, "next_step",
                        statement, 0.7, index + 1, role);
                }
            }
        }

        error.clear();
        return true;
    }
    catch (const std::exception& exception) {
        error = std::string("Unable to extract context proposals: ")
            + exception.what();
        return false;
    }
}

}
