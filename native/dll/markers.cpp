#include "markers.hpp"

#include "game.hpp"

#include <algorithm>
#include <vector>

namespace oyr::markers {
namespace {

// The save's `quests_completed` if it has any of our markers, else null.
std::vector<std::string>* quests() {
    std::vector<std::string>* all = game::quests_completed();
    if (!all || std::ranges::none_of(*all, [](const std::string& quest) { return quest.starts_with(kPrefix); })) {
        return nullptr;
    }
    return all;
}

std::string joined(std::string_view prefix, std::string_view value) {
    std::string marker(prefix);
    marker += value;
    return marker;
}

}  // namespace

bool mark_save(std::string_view identifier) {
    std::vector<std::string>* all = game::quests_completed();
    if (!all) {
        return false;
    }
    all->push_back(joined(kPrefix, identifier));
    return true;
}

bool in_randovania_save() {
    return quests() != nullptr;
}

bool has(std::string_view marker) {
    const std::vector<std::string>* all = quests();
    return all && std::ranges::find(*all, marker) != all->end();
}

size_t count(std::string_view prefix) {
    const std::vector<std::string>* all = quests();
    if (!all) {
        return 0;
    }
    return static_cast<size_t>(std::ranges::count_if(*all, [prefix](const std::string& quest) { return quest.starts_with(prefix); }));
}

std::optional<std::string> value(std::string_view prefix) {
    if (const std::vector<std::string>* all = quests()) {
        for (const std::string& quest : *all) {
            if (quest.starts_with(prefix)) {
                return quest.substr(prefix.size());
            }
        }
    }
    return std::nullopt;
}

bool add(std::string marker) {
    std::vector<std::string>* all = quests();
    if (!all) {
        return false;
    }
    all->push_back(std::move(marker));
    return true;
}

bool set(std::string_view prefix, std::string_view value) {
    std::vector<std::string>* all = quests();
    if (!all) {
        return false;
    }
    std::string marker = joined(prefix, value);
    for (std::string& quest : *all) {
        if (quest.starts_with(prefix)) {
            quest = std::move(marker);
            return true;
        }
    }
    all->push_back(std::move(marker));
    return true;
}

}  // namespace oyr::markers
