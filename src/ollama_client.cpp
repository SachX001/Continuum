#include "../include/ollama_client.h"
#include "../include/nlohmann/json.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <sstream>

namespace continuum {

using json = nlohmann::json;

namespace {

std::wstring widen_ascii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

bool read_response(HINTERNET request, std::string& response, std::string& error) {
    response.clear();

    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = "Ollama response read failed: "
                + std::to_string(GetLastError());
            return false;
        }

        if (available == 0) {
            return true;
        }

        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(
                request,
                buffer.data(),
                available,
                &read)) {
            error = "Ollama response read failed: "
                + std::to_string(GetLastError());
            return false;
        }

        response.append(buffer.data(), read);
    }
}

}

OllamaClient::OllamaClient(std::string host, unsigned short port)
    : host_(std::move(host)), port_(port) {}

bool OllamaClient::generate_json(
    const std::string& model,
    const std::string& prompt,
    std::string& response_json,
    std::string& error
) const {
    response_json.clear();

    const HINTERNET session = WinHttpOpen(
        L"Continuum/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!session) {
        error = "Unable to initialize WinHTTP: "
            + std::to_string(GetLastError());
        return false;
    }

    const HINTERNET connection = WinHttpConnect(
        session,
        widen_ascii(host_).c_str(),
        port_,
        0
    );

    if (!connection) {
        error = "Unable to connect to Ollama at " + host_ + ":"
            + std::to_string(port_) + ". Is Ollama running?";
        WinHttpCloseHandle(session);
        return false;
    }

    const HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        L"/api/chat",
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );

    if (!request) {
        error = "Unable to create Ollama request: "
            + std::to_string(GetLastError());
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    // Local CPU inference can legitimately take several minutes for a full
    // compilation prompt. WinHTTP's default receive timeout is too short and
    // otherwise reports a misleading timeout before Ollama can return JSON.
    if (!WinHttpSetTimeouts(request, 10000, 10000, 10000, 600000)) {
        error = "Unable to configure Ollama request timeout: "
            + std::to_string(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    const json body = {
        {"model", model},
        {"messages", json::array({
            {{"role", "user"}, {"content", prompt}}
        })},
        {"stream", false},
        {"format", "json"},
        {"options", {
            {"temperature", 0},
            // Artifacts are injected from the local evidence manifest after
            // generation, so the model only needs a compact semantic package.
            {"num_predict", 1200}
        }}
    };
    const std::string payload = body.dump();
    const wchar_t* headers = L"Content-Type: application/json\r\n";

    const BOOL sent = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1L),
        const_cast<char*>(payload.data()),
        static_cast<DWORD>(payload.size()),
        static_cast<DWORD>(payload.size()),
        0
    );

    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        error = "Ollama request failed: "
            + std::to_string(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)
        || status_code < 200
        || status_code >= 300) {
        error = "Ollama returned HTTP status "
            + std::to_string(status_code);
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    std::string response;
    const bool read_success = read_response(request, response, error);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (!read_success) {
        return false;
    }

    try {
        const json parsed = json::parse(response);

        if (parsed.contains("error") && parsed["error"].is_string()) {
            error = "Ollama error: " + parsed["error"].get<std::string>();
            return false;
        }

        if (!parsed.contains("message")
            || !parsed["message"].is_object()
            || !parsed["message"].contains("content")
            || !parsed["message"]["content"].is_string()) {
            error = "Ollama response did not contain message.content.";
            return false;
        }

        response_json = parsed["message"]["content"].get<std::string>();
        error.clear();
        return true;
    }
    catch (const std::exception& exception) {
        error = std::string("Invalid Ollama response: ") + exception.what();
        return false;
    }
}

}
