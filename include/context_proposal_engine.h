#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace continuum {

struct ContextProposal {
    std::string id;
    std::string project_id;
    std::string kind;
    std::string statement;
    std::string status = "proposed";
    double confidence = 0.0;
    std::size_t source_message_index = 0;
    std::string source_role;
    std::string review_note;
    std::string supersedes_proposal_id;
};

class ContextProposalEngine {
public:
    static bool extract_from_file(
        const std::filesystem::path& parsed_path,
        const std::string& project_id,
        std::vector<ContextProposal>& proposals,
        std::string& error
    );
};

}
