#include "warp.hpp"

#include "game.hpp"
#include "log.hpp"
#include "markers.hpp"

#include <charconv>
#include <format>
#include <optional>

namespace oyr::warp {
namespace {

constexpr const char* kCommandWStart = "oyr_warp_to_start";
constexpr const char* kTextKeyStart = "ui_oyr_warp_to_start";
constexpr const char* kTextStart = "Warp to Start";
constexpr const char* kCommandWVillage = "oyr_warp_to_village";
constexpr const char* kTextKeyVillage = "ui_oyr_warp_to_village";
constexpr const char* kTextVillage = "Warp to Village";
constexpr const char* kTextKeyVillageLocked = "ui_oyr_warp_to_village_locked";
constexpr const char* kTextVillageLocked = "Reach the village save point first";
constexpr const char* kTextKeyOk = "ui_oyr_ok";
constexpr const char* kTextOk = "OK";
// The game's own command for going back one menu, as the confirm's "No" runs it.
constexpr const char* kCommandBack = "$back";

const Location kVillage{"hub_village", 4226};

// The pause menu object: its root menu node, whose entries the builder adds.
constexpr uintptr_t kMenuRoot = 0x70;
// A menu node: the command it runs when picked, a std::string.
constexpr uintptr_t kNodeCommand = 0x70;
// Menu node methods, as the slot menu builds "Delete" (Epic 0x2aef5f, 0x2af078).
// vtable+0x38 (Epic 0x2bc0f0): MenuNode*(MenuNode*, const char* command, const char* text, ?, ?) - an entry that runs
// `command` when picked, as the pause menu's "Resume".
using AddEntryFn = void* (*)(void* node, const char* command, const char* text, void* unknown, void* unknown2);
constexpr size_t kAddEntry = 0x38 / sizeof(void*);
// vtable+0x40: MenuNode*(MenuNode*, int32 kind, const char* text, ?, ?) - a child node; kind 0 opens a submenu.
using AddNodeFn = void* (*)(void* node, int32_t kind, const char* text, void* unknown, void* unknown2);
constexpr size_t kAddNode = 0x40 / sizeof(void*);
// vtable+0x48 (Epic 0x2bc1a0): fills a node with the line `title`, `yes` running `command` and `no` going back.
using AddConfirmFn = void (*)(void* node, const char* command, const char* title, const char* yes, const char* no);
constexpr size_t kAddConfirm = 0x48 / sizeof(void*);
// What vtable+0x48 sets on its `title` child to make it a plain line: two bytes of the node set to 1.
constexpr uintptr_t kNodeLabel = 0x250;
constexpr uintptr_t kNodeLabel2 = 0x253;

using UiTextFn = const char** (*)(void* texts, const char** out, int32_t language, const char* key);
using CheckpointSaveFn = void (*)(void* save_manager, void* spawner);
using SaveManagerFn = void (*)(void* save_manager);
using SaveApplyFn = bool (*)(void* save_manager, bool unknown);
using ObjectFn = void (*)(void* object);

using game::game_fn;

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

void add_confirm_entry(void* root, const char* text, const char* command, uint8_t* globals) {
    const char* title = ui_text(globals, "ui_confirm");
    const char* yes = ui_text(globals, "ui_confirm_yes");
    const char* no = ui_text(globals, "ui_confirm_no");

    void* submenu = method<AddNodeFn>(root, kAddNode)(root, 0, text, nullptr, nullptr);
    if (!submenu) {
        return;
    }
    method<AddConfirmFn>(submenu, kAddConfirm)(submenu, command, title, yes, no);
}

void add_message_entry(void* root, const char* text, const char* message, const char* ok) {
    void* submenu = method<AddNodeFn>(root, kAddNode)(root, 0, text, nullptr, nullptr);
    if (!submenu) {
        return;
    }
    void* label = method<AddNodeFn>(submenu, kAddNode)(submenu, 0, message, nullptr, nullptr);
    if (label) {
        game::field<uint8_t>(label, kNodeLabel) = 1;
        game::field<uint8_t>(label, kNodeLabel2) = 1;
    }
    method<AddEntryFn>(submenu, kAddEntry)(submenu, kCommandBack, ok, nullptr, nullptr);
}

std::optional<Location> start_location() {
    std::optional<std::string> rest = markers::value(markers::kStart);
    if (!rest) {
        return std::nullopt;
    }
    size_t colon = rest->rfind(':');
    int32_t spawner = 0;
    if (colon == std::string::npos || colon == 0 ||
        std::from_chars(rest->data() + colon + 1, rest->data() + rest->size(), spawner).ptr != rest->data() + rest->size()) {
        log::info("warp: ignoring the malformed start location {}", *rest);
        return std::nullopt;
    }
    return Location{rest->substr(0, colon), spawner};
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

void warp(uint8_t* globals, const Location& target) {
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

std::string reached_marker(const Location& location) {
    return std::format("{}{}:{}", markers::kReached, location.level, location.spawner);
}

}  // namespace

std::string marker(const Location& location) {
    return std::format("{}{}:{}", markers::kStart, location.level, location.spawner);
}

void add_menu_entry(void* menu) {
    uint8_t* globals = game::globals();
    if (!menu || !game::is_randomizer_game() || !markers::in_randovania_save()) {
        return;
    }

    void* root = static_cast<uint8_t*>(menu) + kMenuRoot;

    game::ensure_ui_string(kTextKeyStart, kTextStart);
    const char* text = ui_text(globals, kTextKeyStart);
    if (text && start_location()) {
        add_confirm_entry(root, text, kCommandWStart, globals);
    }

    game::ensure_ui_string(kTextKeyVillage, kTextVillage);
    text = ui_text(globals, kTextKeyVillage);
    if (!text) {
        return;
    }
    if (markers::has(reached_marker(kVillage))) {
        add_confirm_entry(root, text, kCommandWVillage, globals);
    } else {
        game::ensure_ui_string(kTextKeyVillageLocked, kTextVillageLocked);
        game::ensure_ui_string(kTextKeyOk, kTextOk);
        const char* message = ui_text(globals, kTextKeyVillageLocked);
        const char* ok = ui_text(globals, kTextKeyOk);
        if (message && ok) {
            add_message_entry(root, text, message, ok);
        }
    }
}

void on_tick() {
    uint8_t* globals = game::globals();
    if (!game::is_randomizer_game() || game::field<int32_t>(globals, game::globals_offset::spawner) != kVillage.spawner ||
        game::field<std::string>(globals, game::globals_offset::level) != kVillage.level) {
        return;
    }
    std::string reached = reached_marker(kVillage);
    if (!markers::has(reached) && markers::add(std::move(reached))) {
        log::info("warp: reached {} spawner {}", kVillage.level, kVillage.spawner);
    }
}

bool on_command(void* menu, void* picked) {
    std::optional<Location> target;
    if (!menu || !picked) {
        return false;
    }
    auto& command = game::field<std::string>(picked, kNodeCommand);

    if (command == kCommandWStart) {
        target = start_location();
    } else if (command == kCommandWVillage) {
        target = kVillage;
    }

    if (!target) {
        return false;
    }

    uint8_t* globals = game::globals();
    warp(globals, *target);
    game_fn<ObjectFn>(&game::Build::pause_menu_close)(menu);
    game_fn<ObjectFn>(&game::Build::unpause)(globals);
    return true;
}

}  // namespace oyr::warp
