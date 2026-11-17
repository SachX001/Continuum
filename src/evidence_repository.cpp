#include "../include/evidence_repository.h"
#include "../include/context_compiler.h"
#include "../include/nlohmann/json.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cctype>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <set>
#include <vector>

namespace continuum {

using json = nlohmann::json;

namespace {

std::string read_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Unable to open parsed conversation: " + path.string();
        return {};
    }

    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

std::string content_hash(const std::string& content) {
    // Stable local FNV-1a identity. This is used for duplicate detection, not
    // for security or cryptographic integrity; raw files remain available.
    std::uint64_t hash = 1469598103934665603ULL;

    for (unsigned char byte : content) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

bool exec_sql(sqlite3* database, const char* sql, std::string& error) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);

    if (result != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(database);
        sqlite3_free(message);
        return false;
    }

    return true;
}

bool ensure_column(
    sqlite3* database,
    const char* table,
    const char* column,
    const char* definition,
    std::string& error
) {
    const std::string query =
        std::string("SELECT 1 FROM pragma_table_info('") + table
        + "') WHERE name = '" + column + "' LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database, query.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(database);
        return false;
    }
    const int result = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result == SQLITE_ROW) {
        return true;
    }
    const std::string alter = std::string("ALTER TABLE ") + table
        + " ADD COLUMN " + column + " " + definition + ";";
    return exec_sql(database, alter.c_str(), error);
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(
        statement,
        index,
        value.c_str(),
        static_cast<int>(value.size()),
        SQLITE_TRANSIENT
    ) == SQLITE_OK;
}

std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* value = sqlite3_column_text(statement, index);
    return value ? reinterpret_cast<const char*>(value) : std::string{};
}

bool step_done(sqlite3_stmt* statement, sqlite3* database, std::string& error) {
    if (sqlite3_step(statement) == SQLITE_DONE) {
        return true;
    }

    error = sqlite3_errmsg(database);
    return false;
}

class Statement {
public:
    Statement(sqlite3* database, const char* sql, std::string& error)
        : database_(database) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr)
            != SQLITE_OK) {
            error = sqlite3_errmsg(database);
        }
    }

    ~Statement() {
        if (statement_) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const { return statement_; }
    explicit operator bool() const { return statement_ != nullptr; }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_ = nullptr;
};

const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS projects (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS providers (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS accounts (
    id TEXT PRIMARY KEY,
    provider_id TEXT NOT NULL,
    label TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY(provider_id) REFERENCES providers(id)
);
CREATE TABLE IF NOT EXISTS imports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_path TEXT NOT NULL,
    content_hash TEXT NOT NULL UNIQUE,
    parser_version TEXT NOT NULL,
    provider_id TEXT NOT NULL DEFAULT 'claude-desktop',
    account_id TEXT NOT NULL DEFAULT 'local',
    status TEXT NOT NULL,
    created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL,
    import_id INTEGER NOT NULL,
    provider_id TEXT NOT NULL DEFAULT 'claude-desktop',
    account_id TEXT NOT NULL DEFAULT 'local',
    title TEXT NOT NULL,
    source_path TEXT NOT NULL,
    content_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id),
    FOREIGN KEY(import_id) REFERENCES imports(id)
);
CREATE TABLE IF NOT EXISTS messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    ordinal INTEGER NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    content_hash TEXT NOT NULL,
    UNIQUE(conversation_id, ordinal),
    FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);
CREATE TABLE IF NOT EXISTS technical_artifacts (
    id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    source_message_index INTEGER NOT NULL,
    source_role TEXT NOT NULL,
    type TEXT NOT NULL,
    language TEXT NOT NULL,
    status TEXT NOT NULL,
    tested INTEGER NOT NULL,
    content TEXT NOT NULL,
    normalized_content TEXT NOT NULL,
    supersedes_id TEXT,
    revision_of TEXT,
    PRIMARY KEY(conversation_id, id),
    FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);
CREATE INDEX IF NOT EXISTS messages_conversation_idx
    ON messages(conversation_id, ordinal);
CREATE INDEX IF NOT EXISTS artifacts_conversation_idx
    ON technical_artifacts(conversation_id);
CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
    message_id UNINDEXED,
    project_id UNINDEXED,
    conversation_id UNINDEXED,
    ordinal UNINDEXED,
    role UNINDEXED,
    content
);
CREATE TABLE IF NOT EXISTS context_proposals (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL,
    conversation_id TEXT NOT NULL,
    kind TEXT NOT NULL,
    statement TEXT NOT NULL,
    status TEXT NOT NULL,
    confidence REAL NOT NULL,
    source_message_index INTEGER NOT NULL,
    source_role TEXT NOT NULL,
    review_note TEXT NOT NULL DEFAULT '',
    supersedes_proposal_id TEXT,
    created_at TEXT NOT NULL,
    reviewed_at TEXT,
    FOREIGN KEY(project_id) REFERENCES projects(id),
    FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);
CREATE TABLE IF NOT EXISTS context_records (
    id TEXT PRIMARY KEY,
    project_id TEXT NOT NULL,
    proposal_id TEXT NOT NULL UNIQUE,
    kind TEXT NOT NULL,
    statement TEXT NOT NULL,
    status TEXT NOT NULL,
    confidence REAL NOT NULL,
    source_message_index INTEGER NOT NULL,
    source_role TEXT NOT NULL,
    supersedes_record_id TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id),
    FOREIGN KEY(proposal_id) REFERENCES context_proposals(id)
);
CREATE TABLE IF NOT EXISTS proposal_actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    proposal_id TEXT NOT NULL,
    action TEXT NOT NULL,
    note TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY(proposal_id) REFERENCES context_proposals(id)
);
CREATE TABLE IF NOT EXISTS import_diagnostics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    import_id INTEGER NOT NULL,
    severity TEXT NOT NULL,
    code TEXT NOT NULL,
    message TEXT NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY(import_id) REFERENCES imports(id)
);
CREATE TABLE IF NOT EXISTS memory_checkpoints (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id TEXT NOT NULL,
    sequence INTEGER NOT NULL,
    label TEXT NOT NULL,
    memory_hash TEXT NOT NULL,
    created_at TEXT NOT NULL,
    UNIQUE(project_id, sequence),
    FOREIGN KEY(project_id) REFERENCES projects(id)
);
)SQL";

std::string now_text() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    return std::to_string(seconds);
}

bool refresh_normalized_artifacts(sqlite3* database, std::string& error) {
    struct ArtifactRow {
        std::string id;
        std::string conversation_id;
        std::string content;
    };
    std::vector<ArtifactRow> rows;

    {
        Statement select(database,
            "SELECT id, conversation_id, content FROM technical_artifacts;",
            error);
        if (!select) {
            return false;
        }

        while (sqlite3_step(select.get()) == SQLITE_ROW) {
            rows.push_back({
                column_text(select.get(), 0),
                column_text(select.get(), 1),
                column_text(select.get(), 2)
            });
        }
    }

    Statement update(database,
        "UPDATE technical_artifacts SET normalized_content = ? "
        "WHERE id = ? AND conversation_id = ?;",
        error);
    if (!update) {
        return false;
    }

    for (const auto& row : rows) {
        sqlite3_reset(update.get());
        sqlite3_clear_bindings(update.get());
        if (!bind_text(update.get(), 1,
                ContextCompiler::normalize_code_for_display(row.content))
            || !bind_text(update.get(), 2, row.id)
            || !bind_text(update.get(), 3, row.conversation_id)
            || !step_done(update.get(), database, error)) {
            return false;
        }
    }

    return true;
}

bool rebuild_message_search_index(sqlite3* database, std::string& error) {
    return exec_sql(database,
        "DELETE FROM messages_fts;"
        "INSERT INTO messages_fts(message_id, project_id, conversation_id, ordinal, role, content) "
        "SELECT m.id, c.project_id, m.conversation_id, m.ordinal, m.role, m.content "
        "FROM messages m JOIN conversations c ON c.id = m.conversation_id;",
        error);
}

std::string make_fts_query(const std::string& query) {
    std::string result;
    std::string token;

    auto flush = [&]() {
        if (token.empty()) {
            return;
        }
        if (!result.empty()) {
            result += " AND ";
        }
        result += '"' + token + '"';
        token.clear();
    };

    for (const unsigned char character : query) {
        if (std::isalnum(character) || character == '_') {
            token.push_back(static_cast<char>(character));
        } else {
            flush();
        }
    }
    flush();
    return result;
}

std::set<std::string> meaningful_tokens(const std::string& value) {
    std::set<std::string> tokens;
    std::string token;
    const std::set<std::string> stopwords{
        "a", "an", "and", "for", "from", "in", "of", "on", "the",
        "to", "use", "using", "with", "will", "must", "should"
    };
    for (const unsigned char character : value) {
        if (std::isalnum(character)) {
            token.push_back(static_cast<char>(std::tolower(character)));
        } else if (!token.empty()) {
            if (token.size() > 2 && !stopwords.contains(token)) {
                tokens.insert(token);
            }
            token.clear();
        }
    }
    if (token.size() > 2 && !stopwords.contains(token)) {
        tokens.insert(token);
    }
    return tokens;
}

bool statements_conflict(
    const std::string& left,
    const std::string& right
) {
    if (left == right) {
        return false;
    }
    const auto left_tokens = meaningful_tokens(left);
    const auto right_tokens = meaningful_tokens(right);
    std::size_t overlap = 0;
    for (const auto& token : left_tokens) {
        if (right_tokens.contains(token)) {
            ++overlap;
        }
    }
    return overlap >= 2;
}

}

EvidenceRepository::EvidenceRepository(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

EvidenceRepository::~EvidenceRepository() {
    if (database_) {
        sqlite3_close(database_);
    }
}

bool EvidenceRepository::initialize(std::string& error) {
    if (database_) {
        return true;
    }

    const auto parent = database_path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    if (sqlite3_open_v2(
            database_path_.string().c_str(),
            &database_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            nullptr) != SQLITE_OK) {
        error = database_ ? sqlite3_errmsg(database_)
                          : "Unable to open SQLite database.";
        return false;
    }

    if (!exec_sql(database_, "PRAGMA foreign_keys = ON;", error)
        || !exec_sql(database_, schema, error)) {
        return false;
    }

    int schema_version = 0;
    {
        Statement version(database_, "SELECT version FROM schema_version LIMIT 1;", error);
        if (!version) {
            return false;
        }

        if (sqlite3_step(version.get()) == SQLITE_ROW) {
            schema_version = sqlite3_column_int(version.get(), 0);
        } else if (sqlite3_errcode(database_) != SQLITE_DONE) {
            error = sqlite3_errmsg(database_);
            return false;
        }
    }

    if (schema_version == 0) {
        if (!exec_sql(database_, "INSERT INTO schema_version(version) VALUES (7);", error)) {
            return false;
        }
    } else if (schema_version == 1) {
        const char* migration = R"SQL(
            BEGIN IMMEDIATE;
            CREATE TABLE technical_artifacts_v2 (
                id TEXT NOT NULL,
                conversation_id TEXT NOT NULL,
                source_message_index INTEGER NOT NULL,
                source_role TEXT NOT NULL,
                type TEXT NOT NULL,
                language TEXT NOT NULL,
                status TEXT NOT NULL,
                tested INTEGER NOT NULL,
                content TEXT NOT NULL,
                normalized_content TEXT NOT NULL DEFAULT '',
                supersedes_id TEXT,
                revision_of TEXT,
                PRIMARY KEY(conversation_id, id),
                FOREIGN KEY(conversation_id) REFERENCES conversations(id)
            );
            INSERT INTO technical_artifacts_v2
                SELECT id, conversation_id, source_message_index, source_role,
                       type, language, status, tested, content,
                       content,
                       supersedes_id, revision_of
                FROM technical_artifacts;
            DROP TABLE technical_artifacts;
            ALTER TABLE technical_artifacts_v2 RENAME TO technical_artifacts;
            CREATE INDEX IF NOT EXISTS artifacts_conversation_idx
                ON technical_artifacts(conversation_id);
            UPDATE schema_version SET version = 7;
            COMMIT;
        )SQL";

        if (!exec_sql(database_, migration, error)) {
            return false;
        }

        schema_version = 7;
    } else if (schema_version == 2) {
        if (!exec_sql(database_,
                "ALTER TABLE technical_artifacts ADD COLUMN normalized_content TEXT NOT NULL DEFAULT '';"
                "UPDATE technical_artifacts SET normalized_content = content;"
                "UPDATE schema_version SET version = 7;",
                error)) {
            return false;
        }
        schema_version = 7;
    } else if (schema_version == 3) {
        if (!exec_sql(database_,
                "UPDATE schema_version SET version = 7;",
                error)) {
            return false;
        }
        schema_version = 7;
    } else if (schema_version == 4) {
        if (!exec_sql(database_,
                "UPDATE schema_version SET version = 7;",
                error)) {
            return false;
        }
        schema_version = 7;
    }

    if (!ensure_column(database_, "imports", "provider_id",
            "TEXT NOT NULL DEFAULT 'claude-desktop'", error)
        || !ensure_column(database_, "imports", "account_id",
            "TEXT NOT NULL DEFAULT 'local'", error)
        || !ensure_column(database_, "conversations", "provider_id",
            "TEXT NOT NULL DEFAULT 'claude-desktop'", error)
        || !ensure_column(database_, "conversations", "account_id",
            "TEXT NOT NULL DEFAULT 'local'", error)) {
        return false;
    }
    if (!ensure_column(database_, "context_proposals", "supersedes_proposal_id",
            "TEXT", error)
        || !ensure_column(database_, "context_records", "supersedes_record_id",
            "TEXT", error)) {
        return false;
    }
    if (!exec_sql(database_,
            "INSERT OR IGNORE INTO providers(id, name, created_at) "
            "VALUES ('claude-desktop', 'claude-desktop', '0');"
            "INSERT OR IGNORE INTO accounts(id, provider_id, label, created_at) "
            "VALUES ('local', 'claude-desktop', 'local', '0');",
            error)) {
        return false;
    }
    if (schema_version > 0 && schema_version < 7
        && !exec_sql(database_,
            "UPDATE schema_version SET version = 7;", error)) {
        return false;
    }

    if (!refresh_normalized_artifacts(database_, error)) {
        return false;
    }

    if (!rebuild_message_search_index(database_, error)) {
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::import_parsed_conversation(
    const std::filesystem::path& parsed_path,
    const std::string& project_id,
    bool& duplicate,
    std::string& error,
    const std::string& provider_id,
    const std::string& account_id
) {
    duplicate = false;

    if (!initialize(error)) {
        return false;
    }

    const std::string source = read_file(parsed_path, error);
    if (!error.empty()) {
        return false;
    }

    json conversation;
    try {
        conversation = json::parse(source);
    } catch (const std::exception& exception) {
        error = std::string("Parsed conversation is invalid JSON: ")
            + exception.what();
        return false;
    }

    if (!conversation.is_object()
        || !conversation.contains("messages")
        || !conversation["messages"].is_array()) {
        error = "Parsed conversation must contain a messages array.";
        return false;
    }

    if (!conversation.contains("technical_artifacts")) {
        try {
            conversation["technical_artifacts"] = json::parse(
                ContextCompiler::extract_artifacts_json(source)
            );
        }
        catch (...) {
            conversation["technical_artifacts"] = json::array();
        }
    }

    const std::string hash = content_hash(source);
    const std::string conversation_id = "conversation-" + hash;
    const std::string timestamp = now_text();

    if (!exec_sql(database_, "BEGIN IMMEDIATE;", error)) {
        return false;
    }

    auto rollback = [&]() {
        std::string ignored;
        exec_sql(database_, "ROLLBACK;", ignored);
    };

    Statement provider(database_,
        "INSERT OR IGNORE INTO providers(id, name, created_at) VALUES (?, ?, ?);",
        error);
    if (!provider || !bind_text(provider.get(), 1, provider_id)
        || !bind_text(provider.get(), 2, provider_id)
        || !bind_text(provider.get(), 3, timestamp)
        || !step_done(provider.get(), database_, error)) {
        rollback();
        return false;
    }

    Statement account(database_,
        "INSERT OR IGNORE INTO accounts(id, provider_id, label, created_at) "
        "VALUES (?, ?, ?, ?);",
        error);
    if (!account || !bind_text(account.get(), 1, account_id)
        || !bind_text(account.get(), 2, provider_id)
        || !bind_text(account.get(), 3, account_id)
        || !bind_text(account.get(), 4, timestamp)
        || !step_done(account.get(), database_, error)) {
        rollback();
        return false;
    }

    Statement project(database_,
        "INSERT OR IGNORE INTO projects(id, name, created_at) VALUES (?, ?, ?);",
        error);
    if (!project || !bind_text(project.get(), 1, project_id)
        || !bind_text(project.get(), 2, project_id)
        || !bind_text(project.get(), 3, timestamp)
        || !step_done(project.get(), database_, error)) {
        rollback();
        return false;
    }

    Statement import(database_,
        "INSERT OR IGNORE INTO imports(source_path, content_hash, parser_version, "
        "provider_id, account_id, status, created_at) VALUES (?, ?, ?, ?, ?, ?, ?);",
        error);
    if (!import || !bind_text(import.get(), 1, parsed_path.string())
        || !bind_text(import.get(), 2, hash)
        || !bind_text(import.get(), 3, "parsed-conversation-v1")
        || !bind_text(import.get(), 4, provider_id)
        || !bind_text(import.get(), 5, account_id)
        || !bind_text(import.get(), 6, "complete")
        || !bind_text(import.get(), 7, timestamp)
        || !step_done(import.get(), database_, error)) {
        rollback();
        return false;
    }

    if (sqlite3_changes(database_) == 0) {
        rollback();
        duplicate = true;
        error.clear();
        return true;
    }

    const sqlite3_int64 import_id = sqlite3_last_insert_rowid(database_);
    Statement diagnostic(database_,
        "INSERT INTO import_diagnostics(import_id, severity, code, message, created_at) "
        "VALUES (?, ?, ?, ?, ?);",
        error);
    if (!diagnostic) {
        rollback();
        return false;
    }
    auto add_diagnostic = [&](const std::string& severity,
                              const std::string& code,
                              const std::string& message) {
        sqlite3_reset(diagnostic.get());
        sqlite3_clear_bindings(diagnostic.get());
        return sqlite3_bind_int64(diagnostic.get(), 1, import_id) == SQLITE_OK
            && bind_text(diagnostic.get(), 2, severity)
            && bind_text(diagnostic.get(), 3, code)
            && bind_text(diagnostic.get(), 4, message)
            && bind_text(diagnostic.get(), 5, timestamp)
            && step_done(diagnostic.get(), database_, error);
    };
    if (!conversation.contains("title")
        && !add_diagnostic("warning", "MISSING_TITLE",
            "Conversation did not provide a title; a fallback title was used.")) {
        rollback();
        return false;
    }
    for (std::size_t index = 0; index < conversation["messages"].size(); ++index) {
        if (!conversation["messages"][index].contains("role")) {
            if (!add_diagnostic("warning", "MISSING_ROLE",
                    "Message " + std::to_string(index + 1)
                    + " did not provide a role; unknown was used.")) {
                rollback();
                return false;
            }
        }
    }
    if (!conversation.contains("technical_artifacts")
        || !conversation["technical_artifacts"].is_array()
        || conversation["technical_artifacts"].empty()) {
        if (!add_diagnostic("info", "NO_CODE_ARTIFACTS",
                "Conversation contains no extracted technical artifacts.")) {
            rollback();
            return false;
        }
    }
    Statement conversation_statement(database_,
        "INSERT INTO conversations(id, project_id, import_id, provider_id, account_id, "
        "title, source_path, content_hash, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        error);
    if (!conversation_statement
        || !bind_text(conversation_statement.get(), 1, conversation_id)
        || !bind_text(conversation_statement.get(), 2, project_id)
        || sqlite3_bind_int64(conversation_statement.get(), 3, import_id) != SQLITE_OK
        || !bind_text(conversation_statement.get(), 4, provider_id)
        || !bind_text(conversation_statement.get(), 5, account_id)
        || !bind_text(conversation_statement.get(), 6,
            conversation.value("title", "Untitled conversation"))
        || !bind_text(conversation_statement.get(), 7, parsed_path.string())
        || !bind_text(conversation_statement.get(), 8, hash)
        || !bind_text(conversation_statement.get(), 9, timestamp)
        || !step_done(conversation_statement.get(), database_, error)) {
        rollback();
        return false;
    }

    Statement message(database_,
        "INSERT INTO messages(conversation_id, ordinal, role, content, content_hash) "
        "VALUES (?, ?, ?, ?, ?);",
        error);
    if (!message) {
        rollback();
        return false;
    }

    Statement search_message(database_,
        "INSERT INTO messages_fts(message_id, project_id, conversation_id, ordinal, role, content) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        error);
    if (!search_message) {
        rollback();
        return false;
    }

    for (std::size_t index = 0; index < conversation["messages"].size(); ++index) {
        const auto& item = conversation["messages"][index];
        if (!item.is_object() || !item.contains("content")
            || !item["content"].is_string()) {
            error = "Message " + std::to_string(index + 1)
                + " has no string content.";
            rollback();
            return false;
        }

        const std::string content = item["content"].get<std::string>();
        sqlite3_reset(message.get());
        sqlite3_clear_bindings(message.get());
        if (!bind_text(message.get(), 1, conversation_id)
            || sqlite3_bind_int64(message.get(), 2,
                static_cast<sqlite3_int64>(index + 1)) != SQLITE_OK
            || !bind_text(message.get(), 3, item.value("role", "unknown"))
            || !bind_text(message.get(), 4, content)
            || !bind_text(message.get(), 5, content_hash(content))
            || !step_done(message.get(), database_, error)) {
            rollback();
            return false;
        }

        sqlite3_reset(search_message.get());
        sqlite3_clear_bindings(search_message.get());
        if (sqlite3_bind_int64(search_message.get(), 1,
                sqlite3_last_insert_rowid(database_)) != SQLITE_OK
            || !bind_text(search_message.get(), 2, project_id)
            || !bind_text(search_message.get(), 3, conversation_id)
            || sqlite3_bind_int64(search_message.get(), 4,
                static_cast<sqlite3_int64>(index + 1)) != SQLITE_OK
            || !bind_text(search_message.get(), 5, item.value("role", "unknown"))
            || !bind_text(search_message.get(), 6, content)
            || !step_done(search_message.get(), database_, error)) {
            rollback();
            return false;
        }
    }

    if (conversation.contains("technical_artifacts")
        && conversation["technical_artifacts"].is_array()) {
        Statement artifact(database_,
            "INSERT INTO technical_artifacts(id, conversation_id, source_message_index, source_role, type, language, status, tested, content, normalized_content, supersedes_id, revision_of) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            error);
        if (!artifact) {
            rollback();
            return false;
        }

        for (const auto& item : conversation["technical_artifacts"]) {
            if (!item.is_object() || !item.contains("id")
                || !item.contains("content") || !item["id"].is_string()
                || !item["content"].is_string()) {
                error = "Technical artifact has no valid id or content.";
                rollback();
                return false;
            }

            sqlite3_reset(artifact.get());
            sqlite3_clear_bindings(artifact.get());
            if (!bind_text(artifact.get(), 1, item["id"].get<std::string>())
                || !bind_text(artifact.get(), 2, conversation_id)
                || sqlite3_bind_int64(artifact.get(), 3,
                    item.value("source_message_index", 0)) != SQLITE_OK
                || !bind_text(artifact.get(), 4, item.value("source_role", "unknown"))
                || !bind_text(artifact.get(), 5, item.value("type", "code_block"))
                || !bind_text(artifact.get(), 6, item.value("language", "unknown"))
                || !bind_text(artifact.get(), 7, item.value("status", "proposed"))
                || sqlite3_bind_int(artifact.get(), 8,
                    item.value("tested", false) ? 1 : 0) != SQLITE_OK
                || !bind_text(artifact.get(), 9, item["content"].get<std::string>())
                || !bind_text(artifact.get(), 10,
                    item.value("normalized_content",
                        item["content"].get<std::string>()))
                || !bind_text(artifact.get(), 11, item.value("supersedes_id", ""))
                || !bind_text(artifact.get(), 12, item.value("revision_of", ""))
                || !step_done(artifact.get(), database_, error)) {
                rollback();
                return false;
            }
        }
    }

    if (!exec_sql(database_, "COMMIT;", error)) {
        rollback();
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::counts(
    EvidenceCounts& result,
    std::string& error
) const {
    result = {};

    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    for (const auto& query : {
        std::pair<const char*, std::size_t*>(
            "SELECT COUNT(*) FROM conversations;", &result.conversations),
        std::pair<const char*, std::size_t*>(
            "SELECT COUNT(*) FROM messages;", &result.messages),
        std::pair<const char*, std::size_t*>(
            "SELECT COUNT(*) FROM technical_artifacts;", &result.artifacts)
    }) {
        Statement statement(database_, query.first, error);
        if (!statement || sqlite3_step(statement.get()) != SQLITE_ROW) {
            if (error.empty()) {
                error = sqlite3_errmsg(database_);
            }
            return false;
        }
        *query.second = static_cast<std::size_t>(
            sqlite3_column_int64(statement.get(), 0)
        );
    }

    error.clear();
    return true;
}

bool EvidenceRepository::search_messages(
    const std::string& project_id,
    const std::string& query,
    std::size_t limit,
    std::vector<EvidenceSearchResult>& results,
    std::string& error
) const {
    results.clear();

    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }
    if (limit == 0) {
        error = "Search limit must be greater than zero.";
        return false;
    }

    const std::string fts_query = make_fts_query(query);
    if (fts_query.empty()) {
        error = "Search query must contain searchable text.";
        return false;
    }

    Statement statement(database_,
        "SELECT conversation_id, ordinal, role, "
        "snippet(messages_fts, 5, '[', ']', '...', 16) "
        "FROM messages_fts "
        "WHERE project_id = ? AND messages_fts MATCH ? "
        "ORDER BY bm25(messages_fts) LIMIT ?;",
        error);
    if (!statement
        || !bind_text(statement.get(), 1, project_id)
        || !bind_text(statement.get(), 2, fts_query)
        || sqlite3_bind_int64(statement.get(), 3,
            static_cast<sqlite3_int64>(limit)) != SQLITE_OK) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }

    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        results.push_back({
            column_text(statement.get(), 0),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 1)),
            column_text(statement.get(), 2),
            column_text(statement.get(), 3)
        });
    }

    if (step_result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::list_conversations(
    const std::string& project_id,
    std::vector<ConversationSummary>& results,
    std::string& error
) const {
    results.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    Statement statement(database_,
        "SELECT c.id, c.project_id, c.title, c.source_path, "
        "(SELECT COUNT(*) FROM messages m WHERE m.conversation_id = c.id), "
        "(SELECT COUNT(*) FROM technical_artifacts a WHERE a.conversation_id = c.id) "
        "FROM conversations c WHERE c.project_id = ? ORDER BY c.created_at, c.id;",
        error);
    if (!statement || !bind_text(statement.get(), 1, project_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }

    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        results.push_back({
            column_text(statement.get(), 0),
            column_text(statement.get(), 1),
            column_text(statement.get(), 2),
            column_text(statement.get(), 3),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 4)),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 5))
        });
    }
    if (step_result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::inspect_conversation(
    const std::string& conversation_id,
    ConversationSummary& summary,
    std::vector<MessageRecord>& messages,
    std::vector<ArtifactSummary>& artifacts,
    std::string& error
) const {
    messages.clear();
    artifacts.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    Statement conversation(database_,
        "SELECT c.id, c.project_id, c.title, c.source_path, "
        "(SELECT COUNT(*) FROM messages m WHERE m.conversation_id = c.id), "
        "(SELECT COUNT(*) FROM technical_artifacts a WHERE a.conversation_id = c.id) "
        "FROM conversations c WHERE c.id = ?;",
        error);
    if (!conversation || !bind_text(conversation.get(), 1, conversation_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    if (sqlite3_step(conversation.get()) != SQLITE_ROW) {
        error = "Conversation not found: " + conversation_id;
        return false;
    }
    summary = {
        column_text(conversation.get(), 0),
        column_text(conversation.get(), 1),
        column_text(conversation.get(), 2),
        column_text(conversation.get(), 3),
        static_cast<std::size_t>(sqlite3_column_int64(conversation.get(), 4)),
        static_cast<std::size_t>(sqlite3_column_int64(conversation.get(), 5))
    };

    Statement message(database_,
        "SELECT ordinal, role, content FROM messages "
        "WHERE conversation_id = ? ORDER BY ordinal;",
        error);
    if (!message || !bind_text(message.get(), 1, conversation_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    while (sqlite3_step(message.get()) == SQLITE_ROW) {
        messages.push_back({
            static_cast<std::size_t>(sqlite3_column_int64(message.get(), 0)),
            column_text(message.get(), 1),
            column_text(message.get(), 2)
        });
    }

    Statement artifact(database_,
        "SELECT id, source_message_index, language, status, tested, "
        "supersedes_id, revision_of FROM technical_artifacts "
        "WHERE conversation_id = ? ORDER BY source_message_index, id;",
        error);
    if (!artifact || !bind_text(artifact.get(), 1, conversation_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    while (sqlite3_step(artifact.get()) == SQLITE_ROW) {
        artifacts.push_back({
            column_text(artifact.get(), 0),
            static_cast<std::size_t>(sqlite3_column_int64(artifact.get(), 1)),
            column_text(artifact.get(), 2),
            column_text(artifact.get(), 3),
            sqlite3_column_int(artifact.get(), 4) != 0,
            column_text(artifact.get(), 5),
            column_text(artifact.get(), 6)
        });
    }

    error.clear();
    return true;
}

bool EvidenceRepository::backup_to(
    const std::filesystem::path& backup_path,
    std::string& error
) const {
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    const auto parent = backup_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    sqlite3* backup_database = nullptr;
    if (sqlite3_open_v2(
            backup_path.string().c_str(),
            &backup_database,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            nullptr) != SQLITE_OK) {
        error = backup_database ? sqlite3_errmsg(backup_database)
                                : "Unable to create backup database.";
        if (backup_database) {
            sqlite3_close(backup_database);
        }
        return false;
    }

    sqlite3_backup* backup = sqlite3_backup_init(
        backup_database, "main", database_, "main");
    if (!backup) {
        error = sqlite3_errmsg(backup_database);
        sqlite3_close(backup_database);
        return false;
    }

    const int step_result = sqlite3_backup_step(backup, -1);
    const int finish_result = sqlite3_backup_finish(backup);
    if (step_result != SQLITE_DONE && finish_result != SQLITE_OK) {
        error = sqlite3_errmsg(backup_database);
        sqlite3_close(backup_database);
        return false;
    }

    sqlite3_close(backup_database);
    error.clear();
    return true;
}

bool EvidenceRepository::restore_from(
    const std::filesystem::path& backup_path,
    std::string& error
) {
    if (!std::filesystem::exists(backup_path)) {
        error = "Backup file does not exist: " + backup_path.string();
        return false;
    }
    if (std::filesystem::absolute(backup_path)
        == std::filesystem::absolute(database_path_)) {
        error = "Restore source must differ from the active database.";
        return false;
    }

    sqlite3* source = nullptr;
    if (sqlite3_open_v2(
            backup_path.string().c_str(),
            &source,
            SQLITE_OPEN_READONLY,
            nullptr) != SQLITE_OK) {
        error = source ? sqlite3_errmsg(source) : "Unable to open backup.";
        if (source) {
            sqlite3_close(source);
        }
        return false;
    }
    const bool valid = sqlite3_exec(
        source, "PRAGMA integrity_check;", nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(source);
    if (!valid) {
        error = "Backup failed SQLite integrity check.";
        return false;
    }

    if (database_) {
        sqlite3_close(database_);
        database_ = nullptr;
    }
    try {
        std::filesystem::copy_file(
            backup_path,
            database_path_,
            std::filesystem::copy_options::overwrite_existing
        );
    }
    catch (const std::exception& exception) {
        error = std::string("Unable to restore backup: ") + exception.what();
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::find_conversation_id(
    const std::filesystem::path& source_path,
    const std::string& project_id,
    std::string& conversation_id,
    std::string& error
) const {
    conversation_id.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    Statement statement(database_,
        "SELECT id FROM conversations WHERE (source_path = ? OR source_path = ?) "
        "AND project_id = ? ORDER BY created_at DESC, id DESC LIMIT 1;",
        error);
    if (!statement
        || !bind_text(statement.get(), 1, source_path.string())
        || !bind_text(statement.get(), 2,
            std::filesystem::path(source_path).make_preferred().string())
        || !bind_text(statement.get(), 3, project_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    if (sqlite3_step(statement.get()) == SQLITE_ROW) {
        conversation_id = column_text(statement.get(), 0);
    }
    error.clear();
    return true;
}

bool EvidenceRepository::store_proposals(
    const std::string& conversation_id,
    const std::vector<ContextProposal>& proposals,
    std::string& error
) {
    if (!initialize(error)) {
        return false;
    }
    if (!exec_sql(database_, "BEGIN IMMEDIATE;", error)) {
        return false;
    }
    auto rollback = [&]() {
        std::string ignored;
        exec_sql(database_, "ROLLBACK;", ignored);
    };

    Statement statement(database_,
        "INSERT OR IGNORE INTO context_proposals(" 
        "id, project_id, conversation_id, kind, statement, status, confidence, "
        "source_message_index, source_role, review_note, created_at) "
        "VALUES (?, ?, ?, ?, ?, 'proposed', ?, ?, ?, '', ?);",
        error);
    if (!statement) {
        rollback();
        return false;
    }

    const std::string timestamp = now_text();
    for (const auto& proposal : proposals) {
        sqlite3_reset(statement.get());
        sqlite3_clear_bindings(statement.get());
        if (!bind_text(statement.get(), 1, proposal.id)
            || !bind_text(statement.get(), 2, proposal.project_id)
            || !bind_text(statement.get(), 3, conversation_id)
            || !bind_text(statement.get(), 4, proposal.kind)
            || !bind_text(statement.get(), 5, proposal.statement)
            || sqlite3_bind_double(statement.get(), 6, proposal.confidence) != SQLITE_OK
            || sqlite3_bind_int64(statement.get(), 7,
                static_cast<sqlite3_int64>(proposal.source_message_index)) != SQLITE_OK
            || !bind_text(statement.get(), 8, proposal.source_role)
            || !bind_text(statement.get(), 9, timestamp)
            || !step_done(statement.get(), database_, error)) {
            rollback();
            return false;
        }
    }

    if (!exec_sql(database_, "COMMIT;", error)) {
        rollback();
        return false;
    }
    error.clear();
    return true;
}

bool EvidenceRepository::list_proposals(
    const std::string& project_id,
    const std::string& status,
    std::vector<ContextProposal>& proposals,
    std::string& error
) const {
    proposals.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    const char* filtered_sql =
        "SELECT id, project_id, kind, statement, status, confidence, "
        "source_message_index, source_role, review_note, supersedes_proposal_id "
        "FROM context_proposals WHERE project_id = ? AND status = ? "
        "ORDER BY created_at, id;";
    const char* all_sql =
        "SELECT id, project_id, kind, statement, status, confidence, "
        "source_message_index, source_role, review_note, supersedes_proposal_id "
        "FROM context_proposals WHERE project_id = ? ORDER BY created_at, id;";
    Statement statement(database_, status.empty() ? all_sql : filtered_sql, error);
    if (!statement || !bind_text(statement.get(), 1, project_id)
        || (!status.empty() && !bind_text(statement.get(), 2, status))) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }

    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        proposals.push_back({
            column_text(statement.get(), 0),
            column_text(statement.get(), 1),
            column_text(statement.get(), 2),
            column_text(statement.get(), 3),
            column_text(statement.get(), 4),
            sqlite3_column_double(statement.get(), 5),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 6)),
            column_text(statement.get(), 7),
            column_text(statement.get(), 8),
            column_text(statement.get(), 9)
        });
    }
    if (step_result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    error.clear();
    return true;
}

bool EvidenceRepository::review_proposal(
    const std::string& proposal_id,
    const std::string& decision,
    const std::string& review_note,
    std::string& error,
    const std::string& supersedes_proposal_id
) {
    if (decision != "accepted" && decision != "rejected"
        && decision != "supersede") {
        error = "Proposal decision must be accepted, rejected, or supersede.";
        return false;
    }
    if (decision == "supersede" && supersedes_proposal_id.empty()) {
        error = "Supersede review requires a target proposal id.";
        return false;
    }
    if (!initialize(error)) {
        return false;
    }
    if (!exec_sql(database_, "BEGIN IMMEDIATE;", error)) {
        return false;
    }
    auto rollback = [&]() {
        std::string ignored;
        exec_sql(database_, "ROLLBACK;", ignored);
    };

    ContextProposal proposal;
    {
        Statement select(database_,
            "SELECT id, project_id, kind, statement, confidence, "
            "source_message_index, source_role FROM context_proposals WHERE id = ?;",
            error);
        if (!select || !bind_text(select.get(), 1, proposal_id)
            || sqlite3_step(select.get()) != SQLITE_ROW) {
            if (error.empty()) {
                error = "Proposal not found: " + proposal_id;
            }
            rollback();
            return false;
        }
        proposal.id = column_text(select.get(), 0);
        proposal.project_id = column_text(select.get(), 1);
        proposal.kind = column_text(select.get(), 2);
        proposal.statement = column_text(select.get(), 3);
        proposal.confidence = sqlite3_column_double(select.get(), 4);
        proposal.source_message_index = static_cast<std::size_t>(sqlite3_column_int64(select.get(), 5));
        proposal.source_role = column_text(select.get(), 6);
    }

    std::string superseded_record_id;
    if (decision == "accepted") {
        Statement duplicate(database_,
            "SELECT id FROM context_records WHERE project_id = ? "
            "AND kind = ? AND status = 'accepted' AND statement = ? LIMIT 1;",
            error);
        if (!duplicate || !bind_text(duplicate.get(), 1, proposal.project_id)
            || !bind_text(duplicate.get(), 2, proposal.kind)
            || !bind_text(duplicate.get(), 3, proposal.statement)) {
            rollback();
            return false;
        }
        if (sqlite3_step(duplicate.get()) == SQLITE_ROW) {
            error = "Duplicate accepted context record already exists.";
            rollback();
            return false;
        }

        Statement conflicts(database_,
            "SELECT id, statement FROM context_records WHERE project_id = ? "
            "AND kind = ? AND status = 'accepted';",
            error);
        if (!conflicts || !bind_text(conflicts.get(), 1, proposal.project_id)
            || !bind_text(conflicts.get(), 2, proposal.kind)) {
            rollback();
            return false;
        }
        while (sqlite3_step(conflicts.get()) == SQLITE_ROW) {
            const std::string record_id = column_text(conflicts.get(), 0);
            const std::string existing_statement = column_text(conflicts.get(), 1);
            if (statements_conflict(proposal.statement, existing_statement)) {
                error = "Conflicting accepted context record: " + record_id
                    + ". Use supersede with an explicit target.";
                rollback();
                return false;
            }
        }
    } else if (decision == "supersede") {
        Statement target(database_,
            "SELECT id, project_id, kind FROM context_records "
            "WHERE proposal_id = ? AND status = 'accepted';",
            error);
        if (!target || !bind_text(target.get(), 1, supersedes_proposal_id)
            || sqlite3_step(target.get()) != SQLITE_ROW) {
            error = "Accepted supersession target not found: "
                + supersedes_proposal_id;
            rollback();
            return false;
        }
        const std::string target_project = column_text(target.get(), 1);
        const std::string target_kind = column_text(target.get(), 2);
        if (target_project != proposal.project_id || target_kind != proposal.kind) {
            error = "Supersession target must belong to the same project and kind.";
            rollback();
            return false;
        }
        superseded_record_id = column_text(target.get(), 0);
    }

    const std::string timestamp = now_text();
    const std::string stored_status =
        decision == "supersede" ? "accepted" : decision;
    Statement update(database_,
        "UPDATE context_proposals SET status = ?, review_note = ?, reviewed_at = ?, "
        "supersedes_proposal_id = ? WHERE id = ?;", error);
    if (!update || !bind_text(update.get(), 1, stored_status)
        || !bind_text(update.get(), 2, review_note)
        || !bind_text(update.get(), 3, timestamp)
        || !bind_text(update.get(), 4, supersedes_proposal_id)
        || !bind_text(update.get(), 5, proposal_id)
        || !step_done(update.get(), database_, error)) {
        rollback();
        return false;
    }

    Statement action(database_,
        "INSERT INTO proposal_actions(proposal_id, action, note, created_at) "
        "VALUES (?, ?, ?, ?);", error);
    if (!action || !bind_text(action.get(), 1, proposal_id)
            || !bind_text(action.get(), 2, decision)
        || !bind_text(action.get(), 3, review_note)
        || !bind_text(action.get(), 4, timestamp)
        || !step_done(action.get(), database_, error)) {
        rollback();
        return false;
    }

    if (decision == "accepted" || decision == "supersede") {
        Statement record(database_,
            "INSERT OR IGNORE INTO context_records(" 
            "id, project_id, proposal_id, kind, statement, status, confidence, "
            "source_message_index, source_role, supersedes_record_id, created_at) "
            "VALUES (?, ?, ?, ?, ?, 'accepted', ?, ?, ?, ?, ?);", error);
        if (!record || !bind_text(record.get(), 1, "record-" + proposal_id)
            || !bind_text(record.get(), 2, proposal.project_id)
            || !bind_text(record.get(), 3, proposal.id)
            || !bind_text(record.get(), 4, proposal.kind)
            || !bind_text(record.get(), 5, proposal.statement)
            || sqlite3_bind_double(record.get(), 6, proposal.confidence) != SQLITE_OK
            || sqlite3_bind_int64(record.get(), 7,
                static_cast<sqlite3_int64>(proposal.source_message_index)) != SQLITE_OK
            || !bind_text(record.get(), 8, proposal.source_role)
            || !bind_text(record.get(), 9, superseded_record_id)
            || !bind_text(record.get(), 10, timestamp)
            || !step_done(record.get(), database_, error)) {
            rollback();
            return false;
        }
    }

    if (decision == "supersede") {
        Statement supersede(database_,
            "UPDATE context_records SET status = 'superseded' WHERE id = ?;",
            error);
        if (!supersede || !bind_text(supersede.get(), 1, superseded_record_id)
            || !step_done(supersede.get(), database_, error)) {
            rollback();
            return false;
        }
    }

    if (!exec_sql(database_, "COMMIT;", error)) {
        rollback();
        return false;
    }
    error.clear();
    return true;
}

bool EvidenceRepository::list_context_records(
    const std::string& project_id,
    bool include_superseded,
    std::vector<ContextRecord>& records,
    std::string& error
) const {
    records.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    const char* sql = include_superseded
        ? "SELECT id, project_id, proposal_id, kind, statement, status, "
          "confidence, source_message_index, source_role, supersedes_record_id "
          "FROM context_records WHERE project_id = ? ORDER BY created_at, id;"
        : "SELECT id, project_id, proposal_id, kind, statement, status, "
          "confidence, source_message_index, source_role, supersedes_record_id "
          "FROM context_records WHERE project_id = ? AND status = 'accepted' "
          "ORDER BY created_at, id;";
    Statement statement(database_, sql, error);
    if (!statement || !bind_text(statement.get(), 1, project_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }

    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        records.push_back({
            column_text(statement.get(), 0),
            column_text(statement.get(), 1),
            column_text(statement.get(), 2),
            column_text(statement.get(), 3),
            column_text(statement.get(), 4),
            column_text(statement.get(), 5),
            sqlite3_column_double(statement.get(), 6),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 7)),
            column_text(statement.get(), 8),
            column_text(statement.get(), 9)
        });
    }
    if (step_result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    error.clear();
    return true;
}

bool EvidenceRepository::create_checkpoint(
    const std::string& project_id,
    const std::string& label,
    MemoryCheckpoint& checkpoint,
    std::string& error
) {
    checkpoint = {};
    if (!initialize(error)) {
        return false;
    }

    std::vector<ContextRecord> records;
    if (!list_context_records(project_id, false, records, error)) {
        return false;
    }
    std::sort(records.begin(), records.end(),
        [](const ContextRecord& left, const ContextRecord& right) {
            return left.id < right.id;
        });

    std::string snapshot = project_id + "\n";
    for (const auto& record : records) {
        snapshot += record.id + "|" + record.kind + "|"
            + record.statement + "|"
            + std::to_string(record.source_message_index) + "\n";
    }
    const std::string timestamp = now_text();

    Statement sequence(database_,
        "SELECT COALESCE(MAX(sequence), 0) + 1 FROM memory_checkpoints "
        "WHERE project_id = ?;", error);
    if (!sequence || !bind_text(sequence.get(), 1, project_id)
        || sqlite3_step(sequence.get()) != SQLITE_ROW) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    const auto next_sequence = static_cast<std::size_t>(
        sqlite3_column_int64(sequence.get(), 0));
    const std::string memory_hash = content_hash(snapshot);

    Statement insert(database_,
        "INSERT INTO memory_checkpoints(project_id, sequence, label, memory_hash, created_at) "
        "VALUES (?, ?, ?, ?, ?);", error);
    if (!insert || !bind_text(insert.get(), 1, project_id)
        || sqlite3_bind_int64(insert.get(), 2,
            static_cast<sqlite3_int64>(next_sequence)) != SQLITE_OK
        || !bind_text(insert.get(), 3, label)
        || !bind_text(insert.get(), 4, memory_hash)
        || !bind_text(insert.get(), 5, timestamp)
        || !step_done(insert.get(), database_, error)) {
        return false;
    }

    checkpoint = {
        sqlite3_last_insert_rowid(database_), project_id, next_sequence,
        label, memory_hash, timestamp
    };
    error.clear();
    return true;
}

bool EvidenceRepository::list_checkpoints(
    const std::string& project_id,
    std::vector<MemoryCheckpoint>& checkpoints,
    std::string& error
) const {
    checkpoints.clear();
    if (!database_) {
        error = "Evidence repository is not initialized.";
        return false;
    }

    Statement statement(database_,
        "SELECT id, project_id, sequence, label, memory_hash, created_at "
        "FROM memory_checkpoints WHERE project_id = ? ORDER BY sequence;",
        error);
    if (!statement || !bind_text(statement.get(), 1, project_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    int step_result = SQLITE_ROW;
    while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        checkpoints.push_back({
            sqlite3_column_int64(statement.get(), 0),
            column_text(statement.get(), 1),
            static_cast<std::size_t>(sqlite3_column_int64(statement.get(), 2)),
            column_text(statement.get(), 3),
            column_text(statement.get(), 4),
            column_text(statement.get(), 5)
        });
    }
    if (step_result != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }
    error.clear();
    return true;
}

bool EvidenceRepository::build_project_memory_prompt(
    const std::string& project_id,
    const std::string& task,
    const std::filesystem::path& output_path,
    std::string& error
) const {
    std::vector<ContextRecord> records;
    if (!list_context_records(project_id, false, records, error)) {
        return false;
    }

    json selected_artifacts = json::array();
    Statement artifact_statement(database_,
        "SELECT DISTINCT a.id, a.type, a.source_role, a.language, "
        "a.source_message_index, a.status, a.tested, a.content, "
        "a.normalized_content, a.supersedes_id, a.revision_of "
        "FROM technical_artifacts a "
        "JOIN context_proposals p ON p.conversation_id = a.conversation_id "
        "JOIN context_records r ON r.proposal_id = p.id "
        "WHERE r.project_id = ? AND r.status = 'accepted' "
        "ORDER BY a.source_message_index, a.id;",
        error);
    if (!artifact_statement || !bind_text(artifact_statement.get(), 1, project_id)) {
        if (error.empty()) {
            error = sqlite3_errmsg(database_);
        }
        return false;
    }
    int artifact_step = SQLITE_ROW;
    while ((artifact_step = sqlite3_step(artifact_statement.get())) == SQLITE_ROW) {
        json artifact = {
            {"id", column_text(artifact_statement.get(), 0)},
            {"type", column_text(artifact_statement.get(), 1)},
            {"source_role", column_text(artifact_statement.get(), 2)},
            {"language", column_text(artifact_statement.get(), 3)},
            {"source_message_index", sqlite3_column_int64(artifact_statement.get(), 4)},
            {"status", column_text(artifact_statement.get(), 5)},
            {"tested", sqlite3_column_int(artifact_statement.get(), 6) != 0},
            {"content", column_text(artifact_statement.get(), 7)},
            {"normalized_content", column_text(artifact_statement.get(), 8)},
            {"supersedes_id", column_text(artifact_statement.get(), 9)},
            {"revision_of", column_text(artifact_statement.get(), 10)}
        };
        selected_artifacts.push_back(artifact);
    }
    if (artifact_step != SQLITE_DONE) {
        error = sqlite3_errmsg(database_);
        return false;
    }

    try {
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output(output_path);
        if (!output) {
            error = "Unable to create project memory prompt.";
            return false;
        }
        output << "# Continuum Project Memory Compilation\n\n"
               << "You are Continuum's context compiler. Return ONLY a valid "
               << "JSON object with schema_version, project, "
               << "current_objective, current_state, completed_work, "
               << "decisions, constraints, unresolved_issues, next_steps, "
               << "assumptions, technical_artifacts, and source_metadata. "
               << "Use the reviewed memory as current state. Do not treat "
               << "proposed or superseded records as current. The exact "
               << "technical_artifacts manifest is local evidence and will "
               << "be injected after generation; do not reproduce code in "
               << "your JSON response.\n\n"
               << "Project: " << project_id << "\n"
               << "Task: " << (task.empty() ? "Continue the project." : task)
               << "\n\n"
               << "## Accepted Current Context\n\n";
        for (const auto& record : records) {
            output << "- **" << record.kind << "**: " << record.statement
                   << " (source message " << record.source_message_index
                   << ", record " << record.id << ")\n";
        }
        output << "\n## Memory Rules\n\n"
               << "Use only accepted records as current memory. Superseded "
               << "records remain historical evidence and must not be treated "
               << "as current decisions.\n\n"
               << "DETECTED TECHNICAL ARTIFACTS (PRESERVE IN technical_artifacts):\n"
               << selected_artifacts.dump(2) << '\n';
        if (!output) {
            error = "Failed while writing project memory prompt.";
            return false;
        }
    }
    catch (const std::exception& exception) {
        error = std::string("Unable to build project memory prompt: ")
            + exception.what();
        return false;
    }

    error.clear();
    return true;
}

bool EvidenceRepository::build_memory_inspection_report(
    const std::string& project_id,
    const std::filesystem::path& output_path,
    std::string& error
) const {
    std::vector<ContextRecord> history;
    std::vector<ContextProposal> proposed;
    std::vector<ContextProposal> accepted_proposals;
    std::vector<MemoryCheckpoint> checkpoints;
    if (!list_context_records(project_id, true, history, error)
        || !list_proposals(project_id, "proposed", proposed, error)
        || !list_proposals(project_id, "accepted", accepted_proposals, error)
        || !list_checkpoints(project_id, checkpoints, error)) {
        return false;
    }

    try {
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output(output_path);
        if (!output) {
            error = "Unable to create memory inspection report.";
            return false;
        }

        output << "# Project Memory Inspection\n\n"
               << "Project: " << project_id << "\n\n"
               << "## Current Accepted Memory\n\n";
        std::size_t current_count = 0;
        for (const auto& record : history) {
            if (record.status == "accepted") {
                ++current_count;
                output << "- **" << record.kind << "**: " << record.statement
                       << " (" << record.id << ")\n";
            }
        }
        if (current_count == 0) {
            output << "_No accepted current records._\n";
        }

        output << "\n## Historical Superseded Memory\n\n";
        std::size_t superseded_count = 0;
        for (const auto& record : history) {
            if (record.status == "superseded") {
                ++superseded_count;
                output << "- **" << record.kind << "**: " << record.statement
                       << " (" << record.id << ")\n";
            }
        }
        if (superseded_count == 0) {
            output << "_No superseded records._\n";
        }

        output << "\n## Review Queue\n\n"
               << "Proposed: " << proposed.size() << "\n"
               << "Accepted proposal actions: " << accepted_proposals.size()
               << "\n";
        for (const auto& proposal : proposed) {
            output << "- [" << proposal.kind << "] "
                   << proposal.statement << " (" << proposal.id << ")\n";
        }

        output << "\n## Checkpoints\n\n";
        if (checkpoints.empty()) {
            output << "_No checkpoints._\n";
        } else {
            for (const auto& checkpoint : checkpoints) {
                output << "- " << checkpoint.sequence << ": "
                       << checkpoint.label << " (`" << checkpoint.memory_hash
                       << "`)\n";
            }
        }

        if (!output) {
            error = "Failed while writing memory inspection report.";
            return false;
        }
    }
    catch (const std::exception& exception) {
        error = std::string("Unable to build memory inspection report: ")
            + exception.what();
        return false;
    }

    error.clear();
    return true;
}

}
