#include "../include/conversation_parser.h"

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace continuum {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(
        std::filesystem::path(path),
        std::ios::binary
    );

    if (!input.is_open()) {
        return {};
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    return buffer.str();
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);

    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        lines.push_back(line);
    }

    return lines;
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");

    return value.substr(
        first,
        last - first + 1
    );
}

std::string escape_json(const std::string& value) {
    std::string result;

    for (const char ch : value) {
        switch (ch) {
            case '"':
                result += "\\\"";
                break;

            case '\\':
                result += "\\\\";
                break;

            case '\n':
                result += "\\n";
                break;

            case '\r':
                result += "\\r";
                break;

            case '\t':
                result += "\\t";
                break;

            default:
                result += ch;
                break;
        }
    }

    return result;
}

void push_message(
    Conversation& conversation,
    MessageRole role,
    const std::string& content
) {
    const std::string cleaned = trim(content);

    if (cleaned.empty()) {
        return;
    }

    conversation.messages.push_back({
        role,
        cleaned
    });
}

}

Conversation ConversationParser::parse_file(
    const std::string& input_path
) {
    Conversation conversation;

    conversation.title = "Untitled Conversation";

    const std::string raw = read_file(input_path);

    if (raw.empty()) {
        return conversation;
    }

    const auto lines = split_lines(raw);

    constexpr const char* user_marker =
        "You said: ";

    constexpr const char* assistant_marker =
        "Claude responded: ";

    constexpr const char* assistant_end_marker =
        "Copy";

    size_t i = 0;

    while (i < lines.size()) {

        /*
         * USER MESSAGE
         *
         * Structure:
         *
         * You said: <message>
         * <duplicate message>
         * timestamp
         * Retry
         * Edit
         * Copy
         */
        if (lines[i].rfind(user_marker, 0) == 0) {

            const std::string content =
                lines[i].substr(
                    std::string(user_marker).length()
                );

            push_message(
                conversation,
                MessageRole::User,
                content
            );

            ++i;
            continue;
        }

        /*
         * ASSISTANT MESSAGE
         *
         * Structure:
         *
         * Claude responded: <opening sentence>
         *
         * Searched the web
         *
         * Searched the web
         *
         * <actual response>
         *
         * Copy
         */
        if (lines[i].rfind(assistant_marker, 0) == 0) {

            std::string opening =
                lines[i].substr(
                    std::string(assistant_marker).length()
                );

            std::vector<std::string> response_lines;

            /*
             * Search for the second occurrence of
             * the opening sentence.
             */
            size_t j = i + 1;
            bool found_actual_response = false;

            // The accessibility tree may repeat the response heading in the
            // short UI preamble, but headings such as "Notes" can also occur
            // later inside the actual answer (including after a code block).
            // Restrict duplicate detection to that initial preamble so a
            // later heading cannot discard an earlier implementation.
            const size_t duplicate_search_end =
                std::min(lines.size(), i + 1 + 12);

            while (j < duplicate_search_end) {

                const std::string preamble_line = trim(lines[j]);
                if (preamble_line == "Copy to clipboard"
                    || preamble_line == "python"
                    || preamble_line == "cpp"
                    || preamble_line == "c++"
                    || preamble_line == "javascript"
                    || preamble_line == "java"
                    || preamble_line == "typescript") {
                    break;
                }

                if (!opening.empty() &&
                    lines[j] == opening) {

                    found_actual_response = true;
                    ++j;
                    break;
                }

                ++j;
            }

            /*
             * If the accessibility tree does not contain
             * the duplicated opening sentence, fall back
             * to the first line after the marker.
             */
            if (!found_actual_response) {
                response_lines.push_back(opening);
                j = i + 1;
            }

            /*
             * Collect the actual response until the UI
             * "Copy" control.
             */
            while (j < lines.size()) {

                if (lines[j] == assistant_end_marker) {
                    break;
                }

                /*
                 * Don't accidentally consume the next
                 * conversation turn if the UI layout
                 * changes.
                 */
                if (lines[j].rfind(user_marker, 0) == 0 ||
                    lines[j].rfind(assistant_marker, 0) == 0) {
                    break;
                }

                response_lines.push_back(lines[j]);

                ++j;
            }

            std::ostringstream content;

            for (size_t k = 0; k < response_lines.size(); ++k) {
                content << response_lines[k];

                if (k + 1 < response_lines.size()) {
                    content << '\n';
                }
            }

            push_message(
                conversation,
                MessageRole::Assistant,
                content.str()
            );

            i = j;

            if (i < lines.size() &&
                lines[i] == assistant_end_marker) {
                ++i;
            }

            continue;
        }

        ++i;
    }

    return conversation;
}

bool ConversationParser::write_json(
    const Conversation& conversation,
    const std::string& output_path
) {
    std::ofstream output(
        std::filesystem::path(output_path),
        std::ios::binary
    );

    if (!output.is_open()) {
        return false;
    }

    output << "{\n";

    output << "  \"title\": \""
           << escape_json(conversation.title)
           << "\",\n";

    output << "  \"messages\": [\n";

    for (size_t i = 0;
         i < conversation.messages.size();
         ++i) {

        const auto& message =
            conversation.messages[i];

        const char* role = "unknown";

        switch (message.role) {
            case MessageRole::User:
                role = "user";
                break;

            case MessageRole::Assistant:
                role = "assistant";
                break;

            default:
                break;
        }

        output << "    {\n";

        output << "      \"role\": \""
               << role
               << "\",\n";

        output << "      \"content\": \""
               << escape_json(message.content)
               << "\"\n";

        output << "    }";

        if (i + 1 < conversation.messages.size()) {
            output << ",";
        }

        output << "\n";
    }

    output << "  ]\n";
    output << "}\n";

    return true;
}

}
