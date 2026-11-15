#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace oyr::markers {

// Followed by the configuration identifier; new_game writes it when a seed starts.
inline constexpr std::string_view kPrefix = "@open-yoku-rando:";
// Followed by Randovania's layout UUID.
inline constexpr std::string_view kLayoutUuid = "@open-yoku-rando:uuid:";
// Followed by how many remote pickups the save has been given; one entry.
inline constexpr std::string_view kReceived = "@open-yoku-rando:received:";
// Followed by <level>:<spawner>, the seed's start location.
inline constexpr std::string_view kStart = "@open-yoku-rando:start:";
// Followed by <level>:<spawner> of the checkpoint the ball respawns at, once it has been reached.
inline constexpr std::string_view kReached = "@open-yoku-rando:reached:";
// Followed by n, one entry per lit beacon.
inline constexpr std::string_view kBeacon = "@open-yoku-rando:beacon:";
// Followed by an item whose `onpickup` rules were applied.
inline constexpr std::string_view kRules = "@open-yoku-rando:rules:";
inline constexpr std::string_view kVictory = "@open-yoku-rando:victory";

// Turns the loaded save into a Randovania save by adding kPrefix + identifier. Only new_game calls this. Returns false
// if no save is loaded yet.
bool mark_save(std::string_view identifier);

// Whether the loaded save has any entry starting with kPrefix
bool in_randovania_save();
// Whether the save has exactly this entry.
bool has(std::string_view marker);
// How many entries start with prefix.
size_t count(std::string_view prefix);
// The rest of the first entry that starts with prefix; nullopt if there is none.
std::optional<std::string> value(std::string_view prefix);
// Appends marker to the save.
bool add(std::string marker);
// Stores prefix + value as the one entry with this prefix: replaces the first entry starting with prefix, or appends
// it if there is none.
bool set(std::string_view prefix, std::string_view value);

}  // namespace oyr::markers
