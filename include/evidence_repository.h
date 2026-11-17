#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "context_proposal_engine.h"

namespace continuum {

struct EvidenceCounts {
    std::size_t conversations = 0;
    std::size_t messages = 0;
    std::size_t artifacts = 0;
};

struct EvidenceSearchResult {
    std::string conversation_id;
    std::size_t message_ordinal = 0;
    std::string role;
    std::string snippet;
};

struct ConversationSummary {
    std::string id;
    std::string project_id;
    std::string title;
    std::string source_path;
    std::size_t message_count = 0;
    std::size_t artifact_count = 0;
};

struct MessageRecord {
    std::size_t ordinal = 0;
    std::string role;
    std::string content;
};

struct ArtifactSummary {
    std::string id;
    std::size_t source_message_index = 0;
    std::string language;
    std::string status;
    bool tested = false;
    std::string supersedes_id;
    std::string revision_of;
};

struct ContextRecord {
    std::string id;
    std::string project_id;
    std::string proposal_id;
    std::string kind;
    std::string statement;
    std::string status;
    double confidence = 0.0;
    std::size_t source_message_index = 0;
    std::string source_role;
    std::string supersedes_record_id;
};

struct MemoryCheckpoint {
    sqlite3_int64 id = 0;
    std::string project_id;
    std::size_t sequence = 0;
    std::string label;
    std::string memory_hash;
    std::string created_at;
};

class EvidenceRepository {
public:
    explicit EvidenceRepository(std::filesystem::path database_path);
    ~EvidenceRepository();

    EvidenceRepository(const EvidenceRepository&) = delete;
    EvidenceRepository& operator=(const EvidenceRepository&) = delete;

    bool initialize(std::string& error);

    // Imports an existing parsed_conversation.json atomically. The parsed
    // file remains the source evidence; SQLite stores its indexed records.
    // Re-importing identical content is reported as a duplicate and performs
    // no writes.
    bool import_parsed_conversation(
        const std::filesystem::path& parsed_path,
        const std::string& project_id,
        bool& duplicate,
        std::string& error,
        const std::string& provider_id = "claude-desktop",
        const std::string& account_id = "local"
    );

    bool counts(EvidenceCounts& result, std::string& error) const;

    // Searches indexed message text for one project. The FTS index is a
    // derived convenience layer; SQLite evidence remains authoritative.
    bool search_messages(
        const std::string& project_id,
        const std::string& query,
        std::size_t limit,
        std::vector<EvidenceSearchResult>& results,
        std::string& error
    ) const;

    bool list_conversations(
        const std::string& project_id,
        std::vector<ConversationSummary>& results,
        std::string& error
    ) const;

    bool inspect_conversation(
        const std::string& conversation_id,
        ConversationSummary& summary,
        std::vector<MessageRecord>& messages,
        std::vector<ArtifactSummary>& artifacts,
        std::string& error
    ) const;

    bool backup_to(
        const std::filesystem::path& backup_path,
        std::string& error
    ) const;

    bool restore_from(
        const std::filesystem::path& backup_path,
        std::string& error
    );

    bool find_conversation_id(
        const std::filesystem::path& source_path,
        const std::string& project_id,
        std::string& conversation_id,
        std::string& error
    ) const;

    bool store_proposals(
        const std::string& conversation_id,
        const std::vector<ContextProposal>& proposals,
        std::string& error
    );

    bool list_proposals(
        const std::string& project_id,
        const std::string& status,
        std::vector<ContextProposal>& proposals,
        std::string& error
    ) const;

    bool review_proposal(
        const std::string& proposal_id,
        const std::string& decision,
        const std::string& review_note,
        std::string& error,
        const std::string& supersedes_proposal_id = ""
    );

    bool list_context_records(
        const std::string& project_id,
        bool include_superseded,
        std::vector<ContextRecord>& records,
        std::string& error
    ) const;

    bool create_checkpoint(
        const std::string& project_id,
        const std::string& label,
        MemoryCheckpoint& checkpoint,
        std::string& error
    );

    bool list_checkpoints(
        const std::string& project_id,
        std::vector<MemoryCheckpoint>& checkpoints,
        std::string& error
    ) const;

    bool build_project_memory_prompt(
        const std::string& project_id,
        const std::string& task,
        const std::filesystem::path& output_path,
        std::string& error
    ) const;

    bool build_memory_inspection_report(
        const std::string& project_id,
        const std::filesystem::path& output_path,
        std::string& error
    ) const;

private:
    std::filesystem::path database_path_;
    sqlite3* database_ = nullptr;
};

}
