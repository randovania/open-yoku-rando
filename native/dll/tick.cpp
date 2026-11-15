#include "tick.hpp"

#include "game.hpp"
#include "items.hpp"
#include "log.hpp"
#include "lua_bridge.hpp"
#include "messages.hpp"
#include "multiworld.hpp"
#include "new_game.hpp"
#include "overlay.hpp"
#include "warp.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

namespace oyr::tick {
namespace {

// Ticks without a new frame that still count as running, for game_running().
constexpr uint32_t kRunningTicks = 8;

std::atomic<DWORD> g_game_thread = 0;

// Only touched on the game thread.
uint32_t g_last_frame = 0;
bool g_have_frame = false;
uint32_t g_stalled_ticks = kRunningTicks;

// The first thread that ticks is the game thread; ticks on any other thread are ignored.
bool on_game_thread() {
    DWORD thread = GetCurrentThreadId();
    DWORD game_thread = 0;
    if (g_game_thread.compare_exchange_strong(game_thread, thread)) {
        log::info("game thread {}", thread);
        return true;
    }
    return game_thread == thread;
}

void install_overlay_once() {
    static bool tried = false;
    if (tried) {
        return;
    }
    tried = true;
    if (std::string error = overlay::install(); !error.empty()) {
        log::info("no overlay, messages are only logged: {}", error);
    } else {
        log::info("overlay hooked");
    }
}

void count_frames() {
    uint32_t frame = game::frame_counter();
    if (g_have_frame && frame == g_last_frame) {
        g_stalled_ticks = std::min(g_stalled_ticks + 1, kRunningTicks);
    } else {
        g_stalled_ticks = 0;
    }
    g_last_frame = frame;
    g_have_frame = true;
}

}  // namespace

bool game_running() {
    return g_stalled_ticks < kRunningTicks;
}

void on_tick() {
    if (!on_game_thread() || !game::globals()) {
        return;
    }
    install_overlay_once();
    // The UI text loads after the DLL starts, so the menu entry's key is added here.
    if (!game::in_game()) {
        game::ensure_ui_string(new_game::kMenuTextKey, new_game::menu_text());
    }
    game::ensure_dialog_strings(new_game::texts());

    count_frames();
    // A frame ran on this tick: the game is not paused, loading or in a menu.
    const bool frame_ran = g_stalled_ticks == 0;
    // Rechecked at each use, as what runs before it can open a dialog.
    auto quiet = [frame_ran] { return frame_ran && !game::dialog_open(); };

    multiworld::update_inventory_index();
    warp::on_tick();

    items::on_tick(game::in_game() && quiet());
    game::check_item_rules(quiet());
    messages::on_tick(frame_ran);

    // A pickup the game shows no dialog for gets its location's line from here, under the same conditions.
    game::watch_collected_locations();
    if (game::in_game() && quiet()) {
        if (auto dialog = game::pop_location_dialog()) {
            game::show_dialog(dialog->first, dialog->second);
        }
    }

    multiworld::on_tick();
    bridge::run_remote_lua();
}

}  // namespace oyr::tick
