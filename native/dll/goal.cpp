#include "goal.hpp"

#include "game.hpp"
#include "log.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace oyr::goal {
namespace {

constexpr std::string_view kBeaconPrefix = "@open-yoku-rando:beacon:";
constexpr std::string_view kVictory = "@open-yoku-rando:victory";
constexpr std::string_view kFinalBossEnding = "end_1";

}  // namespace

size_t beacons_lit() {
    const std::vector<std::string>* quests = game::quests_completed();
    if (!quests) {
        return 0;
    }
    return static_cast<size_t>(std::ranges::count_if(*quests, [](const std::string& quest) { return quest.starts_with(kBeaconPrefix); }));
}

bool beaten() {
    const std::vector<std::string>* quests = game::quests_completed();
    return quests && std::ranges::find(*quests, kVictory) != quests->end();
}

void on_beacon_used(bool lit_one) {
    if (!lit_one) {
        return;
    }
    std::vector<std::string>* quests = game::randovania_quests();
    if (!quests) {
        return;
    }
    size_t lit = beacons_lit() + 1;
    quests->push_back(std::format("{}{}", kBeaconPrefix, lit));
    log::info("beacon lit, {} so far", lit);
}

void on_achievement(std::string_view name) {
    if (name != kFinalBossEnding || beaten()) {
        return;
    }
    if (std::vector<std::string>* quests = game::randovania_quests()) {
        quests->push_back(std::string(kVictory));
        log::info("the final boss is beaten");
    }
}

}  // namespace oyr::goal
