#pragma once

#include <string>

namespace continuum {

class OllamaClient {
public:
    explicit OllamaClient(
        std::string host = "127.0.0.1",
        unsigned short port = 11434
    );

    bool generate_json(
        const std::string& model,
        const std::string& prompt,
        std::string& response_json,
        std::string& error
    ) const;

private:
    std::string host_;
    unsigned short port_;
};

}
