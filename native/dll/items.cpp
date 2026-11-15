#include "items.hpp"

#include "game.hpp"
#include "log.hpp"
#include "messages.hpp"
#include "multiworld.hpp"
#include "text.hpp"

#include <algorithm>
#include <deque>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::items {
namespace {

// One queued inventory change. Removing is a test operation and shows no message.
struct ItemRequest {
    std::string item;  // on a give, a progression: item ids from the lowest stage up, separated by commas
    std::string text;  // shown as a message once the item is given
    bool remove = false;
    int32_t amount = 0;  // on a remove, how many copies to take; 0 means every copy held
    // On a give from Randovania, the received count and inventory index it expects; dropped on any other value.
    std::optional<size_t> received;
    std::optional<size_t> inventory;
};

// Only touched on the game thread.
std::deque<ItemRequest> g_items;

// The stage of a progression to give: the one above the highest stage held, or the last one again. In Yoku an upgrade
// replaces the stage below it (the game's own pickups do that in 0x16b040), so that stage comes back to be removed.
struct Stage {
    std::string give;
    std::string replaces;
};

Stage next_stage(std::string_view progression) {
    std::vector<std::string_view> fields = text::split(progression, ',');
    std::vector<std::string> stages(fields.begin(), fields.end());
    size_t next = 0;
    for (size_t i = stages.size(); i-- > 0;) {
        if (game::item_count(stages[i]) > 0) {
            next = i + 1;
            break;
        }
    }
    if (next == 0) {
        return {stages.front(), {}};
    }
    if (next == stages.size()) {
        return {stages.back(), {}};
    }
    return {stages[next], stages[next - 1]};
}

// Whether a give from Randovania was named from the state the game is still in.
bool still_current(const ItemRequest& request) {
    if ((request.received || request.inventory) && !multiworld::save_matches()) {
        return false;
    }
    if (request.received && *request.received != multiworld::received_count()) {
        return false;
    }
    return !request.inventory || *request.inventory == multiworld::inventory_index();
}

void remove(const ItemRequest& request) {
    int32_t held = game::item_count(request.item);
    int32_t wanted = request.amount > 0 ? std::min(request.amount, held) : held;
    for (int32_t i = 0; i < wanted; ++i) {
        game::inventory_remove(request.item);
    }
    log::info("removed {} of {}, {} were held", wanted, request.item, held);
}

// False if the request has to wait for another tick.
bool give(ItemRequest& request) {
    if (!still_current(request)) {
        log::info("not giving {}: sent for received {} and inventory {}, the game is at {} and {}", request.item,
                  request.received.value_or(0), request.inventory.value_or(0), multiworld::received_count(),
                  multiworld::inventory_index());
        multiworld::resend_save_state();
        return true;
    }
    // Fruit is no inventory item: the game hands it out as loot, as at a fruit location.
    game::LootResult loot = game::give_loot(request.item);
    if (loot == game::LootResult::Failed) {
        // Not counted; Randovania sends it again after the resend.
        multiworld::resend_save_state();
        return true;
    }
    if (loot == game::LootResult::Later) {
        return false;
    }
    if (loot == game::LootResult::NotLoot) {
        Stage stage = next_stage(request.item);
        log::info("giving {}{}", stage.give, stage.replaces.empty() ? "" : " in place of " + stage.replaces);
        game::inventory_add(stage.give);
        if (!stage.replaces.empty()) {
            game::inventory_remove(stage.replaces);
        }
    }
    // Counted in the save, so a reload does not deliver it again.
    multiworld::note_pickup_received();
    messages::push(request.text);
    return true;
}

}  // namespace

void queue_give(std::string item, std::string text, std::optional<size_t> received, std::optional<size_t> inventory) {
    g_items.push_back({std::move(item), std::move(text), false, 0, received, inventory});
}

void queue_remove(std::string item, int32_t amount) {
    g_items.push_back({std::move(item), {}, true, std::max(amount, 0)});
}

void on_tick(bool can_give) {
    // Only in a level, on a frame the game really ran (not paused) and with no dialog open. A give waits for the
    // message before it, so items arrive one at a time, each with its own message.
    if (g_items.empty() || !can_give || (!g_items.front().remove && messages::busy())) {
        return;
    }
    if (g_items.front().remove) {
        remove(g_items.front());
        g_items.pop_front();
    } else if (give(g_items.front())) {
        g_items.pop_front();
    }
}

}  // namespace oyr::items
