#include "goal.hpp"

#include "log.hpp"
#include "markers.hpp"

#include <format>
#include <string>

namespace oyr::goal {
namespace {

constexpr std::string_view kFinalBossEnding = "end_1";

}  // namespace

size_t beacons_lit() {
    return markers::count(markers::kBeacon);
}

bool beaten() {
    return markers::has(markers::kVictory);
}

void on_beacon_used(bool lit_one) {
    if (!lit_one) {
        return;
    }
    size_t lit = beacons_lit() + 1;
    if (markers::add(std::format("{}{}", markers::kBeacon, lit))) {
        log::info("beacon lit, {} so far", lit);
    }
}

void on_achievement(std::string_view name) {
    if (name != kFinalBossEnding || beaten()) {
        return;
    }
    if (markers::add(std::string(markers::kVictory))) {
        log::info("the final boss is beaten");
    }
}

}  // namespace oyr::goal
