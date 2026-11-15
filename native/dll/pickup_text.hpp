#pragma once

#include <optional>
#include <string>
#include <utility>

// Location texts at pickup, see pickup_text.cpp.
namespace oyr::pickup_text {

// Called before pickup_dialog runs: puts the location's line in place and collapses "nothing_<model>" to "nothing".
void on_bubble_pickup(void* pickup);
// The dialog name pickup_dialog should open; valid until end_bubble_pickup.
const char* pickup_dialog_name(const char* game_name);
void end_bubble_pickup();

// Per tick: queues the line of a location collected without pickup_dialog (a Wickerling).
void watch_collected_locations();
// The next queued line as title and text.
std::optional<std::pair<std::string, std::string>> pop_location_dialog();

}  // namespace oyr::pickup_text
