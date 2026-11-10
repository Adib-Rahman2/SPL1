#pragma once

#include <string>

namespace codecomplex {

enum class Language { Cpp, Java };

inline Language languageForPath(const std::string& path) {
    const std::string ext = ".java";
    if (path.size() < ext.size()) return Language::Cpp;
    for (std::size_t i = 0; i < ext.size(); ++i) {
        char c = path[path.size() - ext.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != ext[i]) return Language::Cpp;
    }
    return Language::Java;
}

}
