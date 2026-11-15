#include "added_locations.hpp"

#include "game.hpp"
#include "log.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace oyr::added_locations {
namespace {

constexpr uintptr_t kStateEntries = 0x88;  // RandomizerState: std::vector<RandomizerEntry>, globals+0x35d8

// The state of an entry that kAddedLocations does not hold: what the seed placed and how far the player got.
struct Kept {
    uint32_t id;
    std::string item;
    bool revealed;
    bool collected;
};

// Filled by before_loader, used and emptied by after_loader.
std::vector<Kept> g_kept;

// The game's entry vector, which the save stores as `_global.randomizer`.
std::vector<game::RandomizerEntry>& entries_of(void* randomizer_state) {
    return game::field<std::vector<game::RandomizerEntry>>(randomizer_state, kStateEntries);
}

// The added location with this entry id, or null for the CSV's own.
const AddedLocation* added(int32_t id) {
    auto found = std::ranges::find(kAddedLocations, static_cast<uint32_t>(id), &AddedLocation::id);
    return found != std::end(kAddedLocations) ? found : nullptr;
}

}  // namespace

// Copies the state of each added entry in the vector, which the loader is about to cut off.
void before_loader(void* randomizer_state) {
    g_kept.clear();
    for (const game::RandomizerEntry& entry : entries_of(randomizer_state)) {
        if (added(entry.id)) {
            g_kept.push_back({static_cast<uint32_t>(entry.id), entry.item, entry.revealed, entry.collected});
        }
    }
}

// Appends each added entry the loader removed.
void after_loader(void* randomizer_state) {
    auto& entries = entries_of(randomizer_state);
    // Empty when the loader bailed out.
    if (entries.empty()) {
        return;
    }
    for (const AddedLocation& location : kAddedLocations) {
        // Normally gone; only if the loader left it in place would it be appended twice.
        if (std::ranges::any_of(entries, [&location](const auto& entry) { return static_cast<uint32_t>(entry.id) == location.id; })) {
            continue;
        }
        game::RandomizerEntry& entry = entries.emplace_back();
        entry.id = static_cast<int32_t>(location.id);
        entry.vanilla_item = location.vanilla_item;
        entry.tracker_x = location.tracker_x;
        entry.tracker_y = location.tracker_y;
        auto kept = std::ranges::find(g_kept, location.id, &Kept::id);
        if (kept != g_kept.end()) {
            entry.item = kept->item;
            entry.revealed = kept->revealed;
            entry.collected = kept->collected;
        } else {
            entry.item = "nothing";
            log::info("location {} is new to this game", location.id);
        }
    }
    g_kept.clear();
}

}  // namespace oyr::added_locations
