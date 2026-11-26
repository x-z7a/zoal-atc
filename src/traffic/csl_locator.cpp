#include "zoal_atc/traffic/csl_locator.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;

bool search(const fs::path &dir, int depth_left) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return false;
    }

    fs::directory_iterator it(dir, ec);
    if (ec) {
        return false;
    }

    while (it != fs::directory_iterator{}) {
        const auto &entry = *it;
        if (entry.is_regular_file(ec)) {
            if (entry.path().filename() == "xsb_aircraft.txt") {
                return true;
            }
        } else if (entry.is_directory(ec)) {
            // Collect subdirectories for recursion
        }

        // One unreadable entry (a broken link) skips that entry, not the search.
        ec.clear();
        it.increment(ec);
        if (ec) {
            return false;
        }
    }

    // Now process subdirectories
    fs::directory_iterator sub_it(dir, ec);
    if (ec) {
        return false;
    }

    while (sub_it != fs::directory_iterator{}) {
        const auto &entry = *sub_it;
        if (entry.is_directory(ec)) {
            if (depth_left > 0) {
                if (search(entry.path(), depth_left - 1)) {
                    return true;
                }
            }
        }

        // One unreadable entry (a broken link) skips that entry, not the search.
        ec.clear();
        sub_it.increment(ec);
        if (ec) {
            return false;
        }
    }

    return false;
}

void discover_helper(const fs::path &dir, int depth, std::vector<zoal_atc::traffic::FoundCsl> &result, const fs::path &plugins_dir) {
    std::error_code ec;

    if (depth > 3) {
        return;
    }

    fs::directory_iterator it(dir, ec);
    if (ec) {
        return;
    }

    while (it != fs::directory_iterator{}) {
        const auto &entry = *it;
        if (entry.is_directory(ec)) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name == "csl") {
                if (zoal_atc::traffic::holds_csl(entry.path())) {
                    result.push_back({entry.path(), entry.path().lexically_relative(plugins_dir).generic_string()});
                }
            } else if (depth < 3) {
                discover_helper(entry.path(), depth + 1, result, plugins_dir);
            }
        }

        // One unreadable entry (a broken link) skips that entry, not the search.
        ec.clear();
        it.increment(ec);
        if (ec) {
            return;
        }
    }
}

} // anonymous namespace

namespace zoal_atc::traffic {

bool holds_csl(const std::filesystem::path &folder, int max_depth) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        return false;
    }
    return search(folder, max_depth);
}

std::vector<std::filesystem::path> default_csl_folders(const std::filesystem::path &plugin_root,
                                                       const std::filesystem::path &plugins_dir) {
    return {
        plugin_root / "Resources" / "CSL",
        plugins_dir / "IVAO_CSL" / "CSL",
        plugins_dir / "LiveTraffic" / "Resources" / "CSL",
        plugins_dir / "xPilot" / "Resources" / "CSL",
        plugins_dir / "X-Ivap Resources" / "CSL"
    };
}

std::vector<FoundCsl> discover_csl_folders(const std::filesystem::path &plugins_dir) {
    std::vector<FoundCsl> result;
    std::error_code ec;

    if (!fs::is_directory(plugins_dir, ec)) {
        return {};
    }

    discover_helper(plugins_dir, 1, result, plugins_dir);

    std::sort(result.begin(), result.end(), [](const FoundCsl &a, const FoundCsl &b) {
        return a.label < b.label;
    });

    return result;
}

std::string normalise_user_path(std::string text, const std::string &home) {
    // Trim leading/trailing whitespace
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.erase(0, 1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }

    // Remove surrounding quotes
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front()) {
        text.erase(0, 1);
        text.pop_back();
        // Trim again after removing quotes
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
            text.erase(0, 1);
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
            text.pop_back();
        }
    }

    // Replace backslash followed by space with just space
    size_t pos = 0;
    while ((pos = text.find("\\ ", pos)) != std::string::npos) {
        text.replace(pos, 2, " ");
        pos++;
    }

    // Handle tilde expansion
    if (!home.empty()) {
        if (text == "~") {
            return home;
        }
        if (text.size() > 1 && text.substr(0, 2) == "~/") {
            return home + text.substr(1);
        }
    }

    return text;
}

CslChoice choose_csl(const std::string &configured, const std::vector<std::filesystem::path> &defaults) {
    CslChoice out;

    if (!configured.empty()) {
        if (holds_csl(fs::path(configured))) {
            out.path = configured;
            out.configured = true;
            return out;
        } else {
            out.skipped = configured + " holds no xsb_aircraft.txt";
        }
    }

    for (const auto &d : defaults) {
        if (holds_csl(d)) {
            out.path = d;
            return out;
        }
    }

    return out;
}

} // namespace zoal_atc::traffic
