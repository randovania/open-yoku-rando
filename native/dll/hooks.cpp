#include "hooks.hpp"

#include "added_locations.hpp"
#include "game.hpp"
#include "goal.hpp"
#include "log.hpp"
#include "lua_bridge.hpp"
#include "new_game.hpp"
#include "pickup_text.hpp"
#include "progressive.hpp"
#include "tick.hpp"
#include "warp.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <format>
#include <initializer_list>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::hooks {
namespace {

// Hooks take and forward all four argument registers, so the exact game signatures don't matter.
using GameFn = uint64_t (*)(void*, void*, void*, void*);

GameFn g_tick = nullptr;
GameFn g_pickup_dialog = nullptr;
GameFn g_randomizer_loader = nullptr;
GameFn g_dialog_init = nullptr;
GameFn g_slot_label_format = nullptr;
GameFn g_beacon_use = nullptr;
GameFn g_pause_menu_command = nullptr;
GameFn g_level_load = nullptr;
GameFn g_texts_set_language = nullptr;  // the trampoline: the replaced first instruction, then the rest of the function

constexpr char kLanguage[] = "en";

// Only English is supported: every language the game asks for loads the English texts instead.
uint64_t hook_texts_set_language(void* texts, void* language, void* c, void* d) {
    const char* asked = static_cast<const char*>(language);
    if (asked && std::strcmp(asked, kLanguage) != 0) {
        log::info("loading the {} texts instead of {}", kLanguage, asked);
    }
    return g_texts_set_language(texts, const_cast<char*>(kLanguage), c, d);
}

// Logs once if the game had loaded its texts in another language before the hook was in place.
void check_text_language() {
    static std::atomic<bool> checked = false;
    if (checked.load() || !game::globals()) {
        return;
    }
    std::string language = game::text_language();
    if (language.empty() || checked.exchange(true)) {
        return;
    }
    if (language != kLanguage) {
        log::info("the texts were loaded in {} before the language hook was installed", language);
    }
}

uint64_t hook_tick(void* a, void* b, void* c, void* d) {
    check_text_language();
    tick::on_tick();
    return g_tick(a, b, c, d);
}

uint64_t hook_pickup_dialog(void* bubble, void* b, void* c, void* d) {
    std::string replaced = progressive::upgrade_pickup_stage(bubble);
    pickup_text::on_bubble_pickup(bubble);
    uint64_t result = g_pickup_dialog(bubble, b, c, d);
    pickup_text::end_bubble_pickup();
    progressive::drop_replaced_stage(replaced);
    return result;
}

// pickup_dialog's dialog_init(table, ?, ?, dialog name): the name may be swapped for the location's own dialog.
uint64_t hook_pickup_dialog_init(void* table, void* b, void* c, void* name) {
    const char* dialog = pickup_text::pickup_dialog_name(static_cast<const char*>(name));
    return g_dialog_init(table, b, c, const_cast<char*>(dialog));
}

// A beacon is lit when its use took the Wickerlings: the game only takes them when it lights it.
uint64_t hook_beacon_use(void* beacon, void* b, void* c, void* d) {
    int32_t before = game::item_count(game::wickerling_inventory_id);
    uint64_t result = g_beacon_use(beacon, b, c, d);
    goal::on_beacon_used(game::item_count(game::wickerling_inventory_id) < before);
    return result;
}

// The pause and main menu's command handler, for the pause menu's warp entries.
uint64_t hook_pause_menu_command(void* menu, void* picked, void* c, void* d) {
    if (warp::on_command(menu, picked)) {
        return 0;
    }
    return g_pause_menu_command(menu, picked, c, d);
}

// Called from the pause menu builder by the stub patch_pause_menu places.
void hook_pause_menu_entries(void* menu) {
    warp::add_menu_entry(menu);
}

// The loadLevel native's level_load: a new game of a seed with a start location begins there instead.
uint64_t hook_load_level_native(void* loader, void* level, void* c, void* d) {
    const char* first = new_game::first_level(static_cast<const char*>(level));
    return g_level_load(loader, const_cast<char*>(first), c, d);
}

uint64_t hook_randomizer_loader(void* raw_state, void* level, void* dump, void* d) {
    auto* state = static_cast<game::RandomizerState*>(raw_state);
    added_locations::before_loader(state);
    bool new_game = new_game::before_loader(state);
    uint64_t result = g_randomizer_loader(state, level, dump, d);
    added_locations::after_loader(state);
    new_game::after_loader(state, new_game);
    return result;
}

// The slot label's sprintf(buf, " (%s: %i)", difficulty, seed): a Randovania seed shows its hash instead.
uint64_t hook_slot_label_format(void* buffer, void* format, void* difficulty, void* seed) {
    if (const std::string* hash = new_game::hash_for_seed(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(seed)))) {
        static const char kFormat[] = " - %s";
        return g_slot_label_format(buffer, const_cast<char*>(kFormat), const_cast<char*>(hash->c_str()), nullptr);
    }
    return g_slot_label_format(buffer, format, difficulty, seed);
}

bool patch(void* address, const void* bytes, size_t size, DWORD protection) {
    DWORD old;
    if (!VirtualProtect(address, size, protection, &old)) {
        return false;
    }
    std::memcpy(address, bytes, size);
    VirtualProtect(address, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    return true;
}

// Game files opened from the mod folder instead when it has them, without touching the install:
// "processed/items/<name>" from "open-yoku-rando/items/<name>" (models for the minted Nothing ids, or a replacement for
// a real item's), the randomizer's script overrides from "open-yoku-rando/randomizer_scripts.csv" and the dialog
// animations from "open-yoku-rando/dialog_anim.strings".
constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kRedirects{{
    {"processed/items/", "open-yoku-rando/items/"},
    {"data/text/randomizer_scripts.csv", "open-yoku-rando/randomizer_scripts.csv"},
    {"processed/text/dialog_anim.strings", "open-yoku-rando/dialog_anim.strings"},
}};

using FopenFn = FILE* (*)(const char* path, const char* mode);
using FopenSFn = errno_t (*)(FILE** file, const char* path, const char* mode);
FopenFn g_fopen = nullptr;
FopenSFn g_fopen_s = nullptr;
std::mutex g_redirect_mutex;
std::set<std::string> g_logged_redirects;

std::string own_path(const char* path) {
    if (!path) {
        return {};
    }
    std::string original = path;
    std::string normalized = original;
    std::ranges::replace(normalized, '\\', '/');
    std::ranges::transform(normalized, normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string own;
    for (const auto& [game_path, mod_path] : kRedirects) {
        if (size_t at = normalized.find(game_path); at != std::string::npos) {
            own = original.substr(0, at) + std::string(mod_path) + original.substr(at + game_path.size());
            break;
        }
    }
    if (own.empty() || GetFileAttributesA(own.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    std::lock_guard lock(g_redirect_mutex);
    if (g_logged_redirects.insert(own).second) {
        log::info("loading {} instead of {}", own, original);
    }
    return own;
}

FILE* hook_fopen(const char* path, const char* mode) {
    std::string own = own_path(path);
    return g_fopen(own.empty() ? path : own.c_str(), mode);
}

errno_t hook_fopen_s(FILE** file, const char* path, const char* mode) {
    std::string own = own_path(path);
    return g_fopen_s(file, own.empty() ? path : own.c_str(), mode);
}

// Points the exe's import of `function` at `hook`; returns what it pointed at before, null if it imports no such name.
void* hook_import(const char* function, void* hook) {
    uint8_t* base = game::base();
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); import->Name; ++import) {
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + import->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + import->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                continue;
            }
            auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(name->Name, function) != 0) {
                continue;
            }
            auto* original = reinterpret_cast<void*>(slots->u1.Function);
            auto target = reinterpret_cast<uintptr_t>(hook);
            return patch(&slots->u1.Function, &target, sizeof(target), PAGE_READWRITE) ? original : nullptr;
        }
    }
    return nullptr;
}

// Space in the exe for absolute jumps (rel32 operands cannot reach the DLL) and for strings the exe is pointed at.
class CodeCave {
  public:
    CodeCave() : next_(game::base() + game::build().code_cave), end_(game::base() + game::build().code_cave_end) {}

    uint8_t* jump_to(void* target) {
        return place(jump_bytes(target));
    }

    // `prefix`, then a jump to target.
    uint8_t* run_then_jump(std::initializer_list<uint8_t> prefix, void* target) {
        return run_then_jump(std::vector<uint8_t>(prefix), target);
    }

    uint8_t* run_then_jump(std::vector<uint8_t> bytes, void* target) {
        auto jump = jump_bytes(target);
        bytes.insert(bytes.end(), jump.begin(), jump.end());
        return place(bytes);
    }

    uint8_t* place(std::span<const uint8_t> bytes) {
        if (next_ + bytes.size() > end_ || !patch(next_, bytes.data(), bytes.size(), PAGE_EXECUTE_READWRITE)) {
            return nullptr;
        }
        uint8_t* address = next_;
        next_ += bytes.size();
        return address;
    }

  private:
    static std::array<uint8_t, 14> jump_bytes(void* target) {
        std::array<uint8_t, 14> stub{0xff, 0x25, 0x00, 0x00, 0x00, 0x00};  // jmp qword ptr [rip+0]
        auto address = reinterpret_cast<uintptr_t>(target);
        std::memcpy(stub.data() + 6, &address, sizeof(address));
        return stub;
    }

    uint8_t* next_;
    uint8_t* end_;
};

// Points the rel32 operand at `operand`, whose instruction ends at `instruction_end`, to `target`.
bool retarget(uintptr_t operand, uintptr_t instruction_end, const void* target) {
    int64_t displacement = reinterpret_cast<int64_t>(target) - reinterpret_cast<int64_t>(game::base() + instruction_end);
    if (displacement < INT32_MIN || displacement > INT32_MAX) {
        return false;
    }
    auto relative = static_cast<int32_t>(displacement);
    return patch(game::base() + operand, &relative, sizeof(relative), PAGE_EXECUTE_READWRITE);
}

// The `call rel32` at site calls stub instead.
bool redirect_call(uintptr_t site, const void* stub) {
    return stub && retarget(site + 1, site + 5, stub);
}

// The 7-byte `lea reg, [rip + rel32]` at `lea` loads target instead.
bool retarget_lea(uintptr_t lea, const void* target) {
    return target && retarget(lea + 3, lea + 7, target);
}

// Replaces the `length` bytes of code at `at` with `jmp rel32` to target, padded with nops.
bool jump_from(uintptr_t at, size_t length, const void* target) {
    std::array<uint8_t, 16> jump{};
    jump.fill(0x90);
    jump[0] = 0xe9;
    return target && length >= 5 && length <= jump.size() &&
           patch(game::base() + at, jump.data(), length, PAGE_EXECUTE_READWRITE) && retarget(at + 1, at + 5, target);
}

// Where the `call rel32` at site goes.
uint8_t* call_target(uintptr_t site) {
    int32_t relative = 0;
    std::memcpy(&relative, game::base() + site + 1, sizeof(relative));
    return game::base() + site + 5 + relative;
}

bool patch_vtable_slot(uintptr_t slot, void* hook) {
    auto value = reinterpret_cast<uintptr_t>(hook);
    return patch(game::base() + slot, &value, sizeof(value), PAGE_READWRITE);
}

// An empty slot gets one entry, "Start Randovania Seed": its first entry gets our text key and `slot_newrandom_`,
// and the "Randomize mode" submenu after it is jumped over to the "Back" entry.
std::string patch_menu(CodeCave& cave) {
    const game::Build& build = game::build();
    std::string_view key = new_game::kMenuTextKey;
    uint8_t* text = cave.place(std::span(reinterpret_cast<const uint8_t*>(key.data()), key.size() + 1));
    if (!retarget_lea(build.menu_start_text, text)) {
        return "cannot point the start entry at its text";
    }
    // Without a seed the entry shows why and gets a command nothing handles.
    new_game::MenuEntry entry = new_game::prepare_menu();
    const uint8_t* command = game::base() + build.menu_newrandom_string;
    if (!entry.startable) {
        std::string_view inert = new_game::kNoSeedCommand;
        command = cave.place(std::span(reinterpret_cast<const uint8_t*>(inert.data()), inert.size() + 1));
    }
    if (!retarget_lea(build.menu_start_command, command)) {
        return "cannot point the start entry at its command";
    }
    // The code after the submenu expects r12 == 0: xor r12d, r12d.
    uint8_t* skip = cave.run_then_jump({0x45, 0x31, 0xe4}, game::base() + build.menu_skip_to);
    if (!skip) {
        return "cannot write to the code cave";
    }
    // To that stub, over the 7-byte mov at menu_skip_from.
    if (!jump_from(build.menu_skip_from, 7, skip)) {
        return "cannot jump over the Randomize mode submenu";
    }
    return {};
}

void append_address(std::vector<uint8_t>& bytes, const void* address) {
    auto value = reinterpret_cast<uintptr_t>(address);
    auto* first = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(value));
}

// The warp entries: the builder calls hook_pause_menu_entries right before its "Exit to Main Menu" entry, and the
// command handler it installs is pointed at hook_pause_menu_command.
std::string patch_pause_menu(CodeCave& cave) {
    const game::Build& build = game::build();
    uint8_t* base = game::base();
    // The menu is in rdi; the stack is 16-byte aligned there. Afterwards the replaced
    // `mov rcx, [rip + globals]` runs as mov rcx, imm64; mov rcx, [rcx].
    std::vector<uint8_t> stub{0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xf9, 0x48, 0xb8};  // sub rsp, 0x20; mov rcx, rdi; mov rax, imm64
    append_address(stub, reinterpret_cast<void*>(&hook_pause_menu_entries));
    stub.insert(stub.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x20, 0x48, 0xb9});  // call rax; add rsp, 0x20; mov rcx, imm64
    append_address(stub, base + build.globals_ptr);
    stub.insert(stub.end(), {0x48, 0x8b, 0x09});  // mov rcx, [rcx]
    uint8_t* entries = cave.run_then_jump(std::move(stub), base + build.pause_menu_exit_entry + 7);
    if (!entries) {
        return "cannot write to the code cave";
    }
    if (!jump_from(build.pause_menu_exit_entry, 7, entries)) {
        return "cannot add the warp entries to the pause menu";
    }
    g_pause_menu_command = reinterpret_cast<GameFn>(base + build.pause_menu_command);
    if (!retarget_lea(build.pause_menu_command_lea, cave.jump_to(reinterpret_cast<void*>(&hook_pause_menu_command)))) {
        return "cannot point the pause menu at the DLL's command handler";
    }
    return {};
}

// texts_set_language starts with the 5-byte mov [rsp + 0x18], rbx
std::string patch_texts_set_language(CodeCave& cave) {
    uintptr_t function = game::build().texts_set_language;
    uint8_t* trampoline = cave.run_then_jump({0x48, 0x89, 0x5c, 0x24, 0x18}, game::base() + function + 5);
    uint8_t* entry = cave.jump_to(reinterpret_cast<void*>(&hook_texts_set_language));
    if (!trampoline || !entry) {
        return "cannot write to the code cave";
    }
    g_texts_set_language = reinterpret_cast<GameFn>(trampoline);
    if (!jump_from(function, 5, entry)) {
        return "cannot hook texts_set_language";
    }
    return {};
}

}  // namespace

std::string install() {
    using game::Build;
    using game::game_fn;
    const Build& build = game::build();
    uint8_t* base = game::base();
    g_tick = game_fn<GameFn>(&Build::tick);
    g_pickup_dialog = game_fn<GameFn>(&Build::pickup_dialog);
    g_randomizer_loader = game_fn<GameFn>(&Build::randomizer_loader);
    g_dialog_init = game_fn<GameFn>(&Build::dialog_init);
    g_beacon_use = game_fn<GameFn>(&Build::beacon_use);
    g_level_load = game_fn<GameFn>(&Build::level_load);
    // The original sprintf, read before the call is redirected.
    g_slot_label_format = reinterpret_cast<GameFn>(call_target(build.slot_label_format_call));

    // First, so as few files as possible are opened before it.
    g_fopen = reinterpret_cast<FopenFn>(hook_import("fopen", reinterpret_cast<void*>(&hook_fopen)));
    g_fopen_s = reinterpret_cast<FopenSFn>(hook_import("fopen_s", reinterpret_cast<void*>(&hook_fopen_s)));
    if (!g_fopen || !g_fopen_s) {
        return "cannot hook the game's fopen and fopen_s imports";
    }

    CodeCave cave;
    if (std::string error = patch_texts_set_language(cave); !error.empty()) {
        return error;
    }
    // Calls in the game that go to the DLL instead.
    const std::pair<uintptr_t, void*> calls[] = {
        {build.randomizer_loader_call, reinterpret_cast<void*>(&hook_randomizer_loader)},
        {build.load_level_native_call, reinterpret_cast<void*>(&hook_load_level_native)},
        {build.slot_label_format_call, reinterpret_cast<void*>(&hook_slot_label_format)},
        {build.pickup_dialog_init_call, reinterpret_cast<void*>(&hook_pickup_dialog_init)},
    };
    for (const auto& [site, hook] : calls) {
        if (!redirect_call(site, cave.jump_to(hook))) {
            return std::format("cannot redirect the call at {:#x}", site);
        }
    }
    if (std::string error = patch_menu(cave); !error.empty()) {
        return error;
    }
    if (std::string error = patch_pause_menu(cave); !error.empty()) {
        return error;
    }
    // Every scripted object's state gets the wrapped unlockAchievement from here on.
    const auto& natives = game::lua_natives();
    auto unlock = std::ranges::find(natives, std::string("unlockAchievement"), &game::LuaNative::name);
    if (unlock == natives.end()) {
        return "the game registers no unlockAchievement native";
    }
    auto wrapped = bridge::wrap_unlock_achievement(reinterpret_cast<bridge::NativeFn>(base + unlock->rva));
    if (!retarget_lea(unlock->registration, cave.jump_to(reinterpret_cast<void*>(wrapped)))) {
        return std::format("cannot point the unlockAchievement registration at {:#x} to the DLL", unlock->registration);
    }

    // Methods the game calls through its vtables; last, so the DLL's code only runs once everything is in place.
    std::vector<std::pair<uintptr_t, void*>> slots;
    for (uintptr_t slot : build.tick_vtable_slots) {
        slots.emplace_back(slot, reinterpret_cast<void*>(&hook_tick));
    }
    slots.emplace_back(build.beacon_use_vtable_slot, reinterpret_cast<void*>(&hook_beacon_use));
    slots.emplace_back(build.pickup_dialog_vtable_slot, reinterpret_cast<void*>(&hook_pickup_dialog));
    for (const auto& [slot, hook] : slots) {
        if (!patch_vtable_slot(slot, hook)) {
            return std::format("cannot patch the vtable slot at {:#x}", slot);
        }
    }
    return {};
}

}  // namespace oyr::hooks
