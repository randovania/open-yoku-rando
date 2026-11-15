#pragma once

#include <cstddef>
#include <string_view>

// The seed's goal, kept as save markers (markers.hpp): kBeacon + n for each lit beacon, which Nim's ceremony checks
// for the n-th one, and kVictory for beating the final boss.
namespace oyr::goal {

// Called after the game's beacon_use: lit_one is true when it took the Wickerlings and lit the beacon.
void on_beacon_used(bool lit_one);
// Called by the game's unlockAchievement native; "end_1" is the ending after the final boss.
void on_achievement(std::string_view name);

size_t beacons_lit();
bool beaten();

}  // namespace oyr::goal
