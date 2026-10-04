#include "config.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ff12 {
namespace {

static std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

bool parseU64(const std::string& text, uint64_t& out) {
    char* end = nullptr;
    errno = 0;
    const char* begin = text.c_str();
    unsigned long long v = std::strtoull(begin, &end, 0);
    if (errno != 0 || end == begin || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

bool parseU32(const std::string& text, uint32_t& out) {
    uint64_t v = 0;
    if (!parseU64(text, v) || v > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseBool(const std::string& text, bool& out) {
    if (text == "1" || text == "true" || text == "TRUE" || text == "on" || text == "ON") { out = true; return true; }
    if (text == "0" || text == "false" || text == "FALSE" || text == "off" || text == "OFF") { out = false; return true; }
    return false;
}

} // namespace

bool LoadConfig(const char* path, Config& cfg, std::string& error) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        error = "cannot open config";
        return false;
    }

    char line[512];
    unsigned line_no = 0;
    while (std::fgets(line, sizeof(line), f)) {
        ++line_no;
        std::string s(line);
        const size_t comment = s.find(';');
        if (comment != std::string::npos) s.resize(comment);
        s = trim(s);
        if (s.empty() || s[0] == '#') continue;

        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(s.substr(0, eq));
        const std::string value = trim(s.substr(eq + 1));

        bool ok = true;
        if (key == "title_id") ok = parseU64(value, cfg.title_id);
        else if (key == "rng_base") {
            cfg.rng_base = value;
            ok = value == "main_pointer" || value == "main" ||
                 value == "heap" || value == "absolute";
        }
        else if (key == "pointer_size") {
            cfg.pointer_size = value;
            ok = value == "auto" || value == "32" || value == "64";
        }
        else if (key == "rng_offset") ok = parseU64(value, cfg.rng_offset);
        else if (key == "state_offset") ok = parseU32(value, cfg.state_offset);
        else if (key == "index_offset") ok = parseU32(value, cfg.index_offset);
        else if (key == "poll_ms") ok = parseU32(value, cfg.poll_ms);
        else if (key == "preview_count") ok = parseU32(value, cfg.preview_count);
        else if (key == "success_min") ok = parseU32(value, cfg.success_min);
        else if (key == "success_max") ok = parseU32(value, cfg.success_max);
        else continue;

        if (!ok) {
            std::fclose(f);
            char msg[128];
            std::snprintf(msg, sizeof(msg), "bad value at line %u", line_no);
            error = msg;
            return false;
        }
    }
    std::fclose(f);

    if (cfg.success_min > 99) cfg.success_min = 99;
    if (cfg.success_max > 99) cfg.success_max = 99;
    if (cfg.success_min > cfg.success_max)
        cfg.success_min = cfg.success_max;
    if (cfg.preview_count == 0) cfg.preview_count = 1;
    if (cfg.preview_count > 64) cfg.preview_count = 64;
    if (cfg.poll_ms < 50) cfg.poll_ms = 50;
    if (cfg.poll_ms > 2000) cfg.poll_ms = 2000;

    return true;
}

} // namespace ff12
