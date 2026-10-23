#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Pause menu warps in a Randovania save: save, then continue at the target.
// Start = the seed's start location (marker set by new_game); Village = hub_village checkpoint 4226, once reached
// (before that, it opens a message with "OK").
namespace oyr::warp {

inline constexpr std::string_view kStartPrefix = "@open-yoku-rando:start:";
inline constexpr std::string_view kReachedPrefix = "@open-yoku-rando:reached:";

struct Location {
    std::string level;
    int32_t spawner = -1;  // object id of a ball_spawner or special_checkpoint; -1 for the level's default spawner
};

// The quests_completed entry that records `location`.
std::string marker(const Location& location);

// Adds the warp entries; the menu builder calls this right before "Exit to Main Menu".
void add_menu_entry(void* menu);
// Called by the menu's command handler; true if `picked` was ours and is done.
bool on_command(void* menu, void* picked);
// Per tick: marks the village checkpoint as reached once the game saved there.
void on_tick();

}  // namespace oyr::warp
