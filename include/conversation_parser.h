#pragma once

#include <string>
#include <vector>

namespace continuum {

enum class MessageRole {
    User,
    Assistant,
    Unknown
};

struct Message {
    MessageRole role;
    std::string content;
};

struct Conversation {
    std::string title;
    std::vector<Message> messages;
};

class ConversationParser {
public:
    static Conversation parse_file(
        const std::string& input_path
    );

    static bool write_json(
        const Conversation& conversation,
        const std::string& output_path
    );
};

}