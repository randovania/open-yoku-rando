#pragma once

// Randomizer entries the game's CSV has no row for. The loader cuts the entry vector back to the CSV's rows on every
// level load, so these are kept aside around it. A script's addItem(vanilla_item) spawns the entry's bubble at its
// object (game_files.py, SCRIPT_PATCHES); that object must not receive impulses, as a scripted one would hand out the
// bubble on its first impulse instead of running its script.

#include <cstdint>

namespace oyr::added_locations {

struct AddedLocation {
    uint32_t id;
    const char* vanilla_item;
    int32_t tracker_x;
    int32_t tracker_y;
};

inline constexpr AddedLocation kAddedLocations[] = {
    {587u << 16 | 2632u, "tunguska_reward", 1808, -396},
};

// Around the randomizer loader; after_loader runs before new_game's, so a new game's placement fills these too.
void before_loader(void* randomizer_state);
void after_loader(void* randomizer_state);

}  // namespace oyr::added_locations
