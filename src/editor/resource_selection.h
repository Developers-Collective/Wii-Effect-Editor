#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace breff {
    struct ResourceSelection {
        std::vector<std::string> names;
        std::string anchor, preview;

        bool contains(const std::string& name) const {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        void single(const std::string& name) {
            names.clear();
            if (!name.empty())
                names.push_back(name);
            anchor = preview = name;
        }

        void select(const std::string& name, const std::vector<std::string>& visible, bool control, bool shift) {
            const auto first = std::find(visible.begin(), visible.end(), anchor);
            const auto last = std::find(visible.begin(), visible.end(), name);
            if (shift && first != visible.end() && last != visible.end()) {
                if (!control)
                    names.clear();
                for (auto item = std::min(first, last); item <= std::max(first, last); ++item)
                    if (!contains(*item))
                        names.push_back(*item);
                // The clicked endpoint is the most recently selected entry.
                std::erase(names, name);
                names.push_back(name);
                preview = name;
            } else if (control) {
                if (contains(name))
                    std::erase(names, name);
                else
                    names.push_back(name);
                anchor = name;
                if (!names.empty())
                    preview = names.back();
            } else
                single(name);
        }

        void reconcile(const std::vector<std::string>& available, const std::string& current) {
            std::erase_if(names, [&](const auto& name) {
                return std::find(available.begin(), available.end(), name) == available.end();
            });
            if (preview != current)
                single(current);
        }
    };
}
