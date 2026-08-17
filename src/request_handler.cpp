#include "request_handler.h"

#include <algorithm>
#include <cctype>

namespace http_handler {

bool IsSubPath(std::filesystem::path path, std::filesystem::path base) {
    path = std::filesystem::weakly_canonical(path);
    base = std::filesystem::weakly_canonical(base);
    for (auto b = base.begin(), p = path.begin(); b != base.end(); ++b, ++p) {
        if (p == path.end() || *p != *b) {
            return false;
        }
    }
    return true;
}

std::string UrlDecode(std::string_view str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex(str[i + 1]), l = hex(str[i + 2]);
            if (h != -1 && l != -1) {
                result += static_cast<char>((h << 4) | l);
                i += 2;
                continue;
            }
        }
        result += str[i];
    }
    return result;
}

std::string GetMimeType(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == ".htm" || ext == ".html") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".json") return "application/json";
    if (ext == ".js") return "text/javascript";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    return "application/octet-stream";
}

json::value GetContentTypeAsJson(const StringResponse& resp) {
    if (auto ct = resp.find(http::field::content_type); ct != resp.end()) {
        return json::value(std::string(ct->value()));
    }
    return json::value(nullptr);
}

std::string DirectionToString(model::Direction dir) {
    switch (dir) {
        case model::Direction::UP:    return "U";
        case model::Direction::DOWN:  return "D";
        case model::Direction::LEFT:  return "L";
        case model::Direction::RIGHT: return "R";
    }
    return "U";
}

}  // namespace http_handler