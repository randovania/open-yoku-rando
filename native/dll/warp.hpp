#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// "Warp to Start" in the pause menu of a Randovania save: saves the game, then continues it at the seed's start
// location. The location is kept in the save's `quests_completed` as "@open-yoku-rando:start:<level>:<spawner>", put
// there by new_game when the seed starts.
namespace oyr::warp {

inline constexpr std::string_view kStartPrefix = "@open-yoku-rando:start:";

struct Location {
    std::string level;
    int32_t spawner = -1;  // object id of a ball_spawner or special_checkpoint; -1 for the level's default spawner
};

// The quests_completed entry that records `location`.
std::string marker(const Location& location);

// Adds "Warp to Start" to the pause menu. The game's menu builder adds its entries one by one and is patched to call
// this between "Restart from Checkpoint" and "Exit to Main Menu", so the entry appears between those two.
void add_menu_entry(void* menu);
// Called by the menu's command handler; true if `picked` was ours and is done.
bool on_command(void* menu, void* picked);

}  // namespace oyr::warp
