// The `onpickup <item>` rules of randomizer_scripts.csv (e.g. the Underbelly key setting "has key"), which the game
// only runs for its own pickups. Run here for held items without a save marker; the marker is set only when applied
// directly. A rule for an unloaded level goes into the game's queue (saved as `randomizer_impulses`) and is run again
// once that queue no longer holds it.
// This also runs after the game's own pickups, because the game runs them only for pickup_item holders, not for
// objectives (trial scarabs) or bubble_spawners.

#include "item_rules.hpp"

#include "game.hpp"
#include "log.hpp"
#include "markers.hpp"

#include <algorithm>
#include <span>
#include <string>

namespace oyr::item_rules {
using namespace game;
namespace {

// Only called once can_run() found the game's globals, so randomizer_state() is not null.
std::span<const ItemRule> rules() {
    return randomizer_state()->item_rules;
}

bool queued_in_game(int32_t target) {
    return std::ranges::any_of(randomizer_state()->pending_actions, [&](const PendingAction& action) { return action.target == target; });
}

bool still_queued(const std::string& item) {
    return std::ranges::any_of(rules(), [&](const ItemRule& rule) { return rule.item == item && queued_in_game(rule.target); });
}

std::string marker(const std::string& item) {
    return std::string(markers::kRules) + item;
}

// item_rule_action finds the target's level on the main map, reached through whichever level is being played.
bool can_run() {
    uint8_t* world = in_game() ? field<uint8_t*>(globals(), globals_offset::world) : nullptr;
    return world && field<void*>(world, world_main_map_offset);
}

void run(const std::string& item) {
    for (const ItemRule& rule : rules()) {
        if (rule.item == item) {
            item_rule_action(randomizer_state(), rule.target, rule.action.c_str());
        }
    }
    if (still_queued(item)) {
        log::info("{}: rules queued until their level loads", item);
    } else {
        log::info("{}: rules applied", item);
        markers::add(marker(item));
    }
}

}  // namespace

void on_tick(bool running) {
    if (!running || !markers::in_randovania_save() || !can_run()) {
        return;
    }
    for (const ItemRule& rule : rules()) {
        const std::string& item = rule.item;
        if (item_count(item) > 0 && !markers::has(marker(item)) && !still_queued(item)) {
            run(item);
        }
    }
}

}  // namespace oyr::item_rules
