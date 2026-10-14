#include "warp.hpp"

#include "game.hpp"
#include "log.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <vector>

namespace oyr::warp {
namespace {

constexpr int32_t kGameModeRandomizer = 3;
constexpr const char* kCommand = "oyr_warp_to_start";
constexpr const char* kTextKey = "ui_oyr_warp_to_start";
constexpr const char* kText = "Warp to Start";

// The pause menu object: its root menu node, whose entries the builder adds.
constexpr uintptr_t kMenuRoot = 0x70;
// A menu node: the command it runs when picked, a std::string.
constexpr uintptr_t kNodeCommand = 0x70;
// Menu node methods, as the slot menu builds "Delete" (Epic 0x2aef5f, 0x2af078).
// vtable+0x40: MenuNode*(MenuNode*, int32 kind, const char* text, ?, ?) - a child node; kind 0 opens a submenu.
using AddNodeFn = void* (*)(void* node, int32_t kind, const char* text, void* unknown, void* unknown2);
constexpr size_t kAddNode = 0x40 / sizeof(void*);
// vtable+0x48 (Epic 0x2bc1a0): fills a node with the line `title`, `yes` running `command` and `no` going back.
using AddConfirmFn = void (*)(void* node, const char* command, const char* title, const char* yes, const char* no);
constexpr size_t kAddConfirm = 0x48 / sizeof(void*);

using UiTextFn = const char** (*)(void* texts, const char** out, int32_t language, const char* key);
using CheckpointSaveFn = void (*)(void* save_manager, void* spawner);
using SaveManagerFn = void (*)(void* save_manager);
using SaveApplyFn = bool (*)(void* save_manager, bool unknown);
using ObjectFn = void (*)(void* object);

template <class Fn>
Fn game_fn(uintptr_t game::Build::* rva) {
    return reinterpret_cast<Fn>(game::base() + game::build().*rva);
}

template <class Fn>
Fn method(void* object, size_t slot) {
    return (*reinterpret_cast<Fn**>(object))[slot];
}

// The text of a "ui_" key in the game's language, falling back to English, where ensure_ui_string adds ours.
const char* ui_text(uint8_t* globals, const char* key) {
    const char* text = nullptr;
    game_fn<UiTextFn>(&game::Build::ui_text)(globals + game::globals_offset::ui_texts, &text, 0, key);
    return text;
}

std::optional<Location> start_location(const std::vector<std::string>& quests) {
    for (const std::string& quest : quests) {
        if (!quest.starts_with(kStartPrefix)) {
            continue;
        }
        std::string_view rest = std::string_view(quest).substr(kStartPrefix.size());
        size_t colon = rest.rfind(':');
        int32_t spawner = 0;
        if (colon == std::string_view::npos || colon == 0 ||
            std::from_chars(rest.data() + colon + 1, rest.data() + rest.size(), spawner).ptr != rest.data() + rest.size()) {
            log::info("warp: ignoring the malformed start location {}", quest);
            continue;
        }
        return Location{std::string(rest.substr(0, colon)), spawner};
    }
    return std::nullopt;
}

// Why the game cannot be saved and continued right now; empty if it can.
std::string cannot_warp(uint8_t* globals) {
    if (!game::field<void*>(globals, game::globals_offset::world)) {
        return "no level is running";
    }
    if (game::field<void*>(globals, game::globals_offset::level_loading)) {
        return "a level is loading";
    }
    if (game::field<uint8_t>(globals, game::globals_offset::save_flags) & 1) {
        return "the game does not save right now";
    }
    return {};
}

void warp(uint8_t* globals) {
    std::vector<std::string>* quests = game::quests_completed();
    if (!quests) {
        return;
    }
    std::optional<Location> start = start_location(*quests);
    if (!start) {
        return;
    }
    const Location& target = *start;
    if (std::string reason = cannot_warp(globals); !reason.empty()) {
        log::info("warp: not warping, {}", reason);
        return;
    }
    // Save first, so the inventory, quests and level states of now are what the game continues from. checkpoint_save
    // records the running level; the target replaces it before _global is written again, then the save is continued
    // like "Restart from Checkpoint" continues the file.
    void* save_manager = globals + game::globals_offset::save_manager;
    std::string from = game::current_level();
    game_fn<CheckpointSaveFn>(&game::Build::checkpoint_save)(save_manager, nullptr);
    game::field<std::string>(globals, game::globals_offset::level) = target.level;
    game::field<int32_t>(globals, game::globals_offset::spawner) = target.spawner;
    game_fn<SaveManagerFn>(&game::Build::save_globals)(save_manager);
    bool loaded = game_fn<SaveApplyFn>(&game::Build::save_apply)(save_manager, true);
    log::info("warp: from {} to {} spawner {}{}", from, target.level, target.spawner, loaded ? "" : ", but the game loaded no level");
}

}  // namespace

std::string marker(const Location& location) {
    return std::format("{}{}:{}", kStartPrefix, location.level, location.spawner);
}

void add_menu_entry(void* menu) {
    uint8_t* globals = game::globals();
    std::vector<std::string>* quests = game::quests_completed();
    if (!globals || !quests || game::field<int32_t>(globals, game::globals_offset::game_mode) != kGameModeRandomizer || !start_location(*quests)) {
        return;
    }
    game::ensure_ui_string(kTextKey, kText);
    const char* text = ui_text(globals, kTextKey);
    const char* title = ui_text(globals, "ui_confirm");
    const char* yes = ui_text(globals, "ui_confirm_yes");
    const char* no = ui_text(globals, "ui_confirm_no");
    void* root = static_cast<uint8_t*>(menu) + kMenuRoot;
    void* submenu = method<AddNodeFn>(root, kAddNode)(root, 0, text, nullptr, nullptr);
    if (!submenu) {
        return;
    }
    method<AddConfirmFn>(submenu, kAddConfirm)(submenu, kCommand, title, yes, no);
}

bool on_command(void* menu, void* picked) {
    if (!picked || game::field<std::string>(picked, kNodeCommand) != kCommand) {
        return false;
    }
    uint8_t* globals = game::globals();
    warp(globals);
    game_fn<ObjectFn>(&game::Build::pause_menu_close)(menu);
    game_fn<ObjectFn>(&game::Build::unpause)(globals);
    return true;
}

}  // namespace oyr::warp
