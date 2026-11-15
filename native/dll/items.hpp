#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Remote inventory changes (RL.GiveItem, RL.RemoveItem), queued and handed out one at a time on the game thread.
namespace oyr::items {

// `item` is a progression: item ids from the lowest stage up, separated by commas. `text` is shown as a message once
// the item is given. A give from Randovania names the received count and inventory index it expects; it is dropped
// on any other value.
void queue_give(std::string item, std::string text, std::optional<size_t> received, std::optional<size_t> inventory);
// Takes `amount` copies; 0 means every copy held. A test operation; it shows no message.
void queue_remove(std::string item, int32_t amount);

// Per tick: handles the first queued request when `can_give` (in a level, on a frame the game ran, no dialog open).
// A give also waits for the message before it, so items arrive one at a time, each with its own message.
void on_tick(bool can_give);

}  // namespace oyr::items
