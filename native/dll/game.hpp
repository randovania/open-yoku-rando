#pragma once

#include "builds.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::game {

// Offsets into the object behind Build::globals_ptr; the same in all builds.
namespace globals_offset {
// The save manager: the save document and the `_global` fields the game keeps outside its own objects.
inline constexpr uintptr_t save_manager = 0x2590;
inline constexpr uintptr_t level = 0x25b8;      // std::string, save_manager+0x28; "_start_screen" in the main menu
inline constexpr uintptr_t spawner = 0x25d8;    // int32, save_manager+0x48: object id the ball starts at in `level`
inline constexpr uintptr_t level_loading = 0x3630;  // non-null while a level load is pending
inline constexpr uintptr_t save_flags = 0x1f06c;    // bit 0: the game saves nothing
inline constexpr uintptr_t ui_texts = 0x8fd0;       // what ui_text looks keys up in; starts with the loaded language
inline constexpr uintptr_t inventory = 0x29e8;  // std::vector<InventoryItem>
// std::vector<std::string>; the game keeps entries it does not know, so the DLL stores its save markers here
inline constexpr uintptr_t quests_completed = 0x2688;
inline constexpr uintptr_t randomizer_entries = 0x35d8;  // std::vector<RandomizerEntry>, in save order
inline constexpr uintptr_t frame_counter = 0x2028;       // uint32
inline constexpr uintptr_t fruit = 0x2a30;               // int32, the save's `fruit`
inline constexpr uintptr_t total_fruit = 0x2a34;         // int32, the save's `total_fruit`
inline constexpr uintptr_t game_mode = 0x93b0;           // int32; kGameModeRandomizer in a randomizer game
inline constexpr uintptr_t randomizer_state = 0x3550;    // RandomizerState, what the randomizer loader gets
inline constexpr uintptr_t dialog_flag = 0x4dd0;
inline constexpr uintptr_t string_table = 0x4de0;
inline constexpr uintptr_t ui_file = 0x9018;
inline constexpr uintptr_t dialog_file = 0x9028;  // English; the DLL keeps the game in English, see hooks.cpp
inline constexpr uintptr_t file_strings = 0x78;  // std::vector<std::array<std::string, 2>> in a text file object
inline constexpr uintptr_t world = 0x1f80;        // World*, the level being played
inline constexpr uintptr_t loot_tables = 0x89f8;  // std::vector<LootTable>, loottables.json
}  // namespace globals_offset

inline constexpr int32_t kGameModeRandomizer = 3;

// World -> the main map, which item_rule_action finds a target's level on.
inline constexpr uintptr_t world_main_map_offset = 0x6e8;

// The ball, as the game reaches it from bubble_pickup's caller and from its position checks.
inline constexpr uintptr_t world_ball_offset = 0x830;  // World -> Object*, the ball
inline constexpr uintptr_t ball_body_offset = 0x18;    // ball -> physics body
inline constexpr uintptr_t body_position_offset = 0xc;  // physics body -> float x, y, in metres
inline constexpr float physics_to_level = 100.0f;      // metres -> level units

// Every level object keeps a weak pointer to itself, which the game locks when the object becomes another's owner.
inline constexpr uintptr_t object_self_control_offset = 0x10;  // _Ref_count_base*; uses at +8
inline constexpr uintptr_t object_level_offset = 0x80;         // the level object

// Box2D: a level's physics world refuses to create bodies while it steps, and the tick can run inside a step.
inline constexpr uintptr_t level_physics_offset = 0x2c0;     // b2World*
inline constexpr uintptr_t physics_flags_offset = 0x19298;   // b2World::m_flags
inline constexpr uint32_t physics_locked = 2;                // e_locked

// Offsets into a pickup_item level object, `this` of pickup_dialog. Its location id is level_number << 16 | id.
namespace pickup_offset {
inline constexpr uintptr_t id = 0x20;     // uint32, the object id within its level
inline constexpr uintptr_t level = 0x80;  // the level object
inline constexpr uintptr_t item = 0x230;  // std::string, the item id
}  // namespace pickup_offset
inline constexpr uintptr_t level_info_offset = 0x6e0;   // level object -> level info
inline constexpr uintptr_t level_number_offset = 0x20;  // level info -> uint32 level number

// Item id of a location holding nothing the game knows; see docs/multiworld-state.md.
inline constexpr const char* nothing_item = "nothing";
// Also true for the "nothing_<model>" ids the patcher mints for a custom bubble model.
bool is_nothing_item(const std::string& item);

// A Wickerling's item id; the game keeps it in the inventory, and its texts, as wickerling_inventory_id.
inline constexpr std::string_view wickerling_item = "wickerling";
inline constexpr const char* wickerling_inventory_id = "collectible";
// The id the game keeps `item` under in its inventory and texts.
const char* inventory_id(const std::string& item);

// One element of globals_offset::randomizer_entries, the save's `_global.randomizer`. Based on YokuArchipelagoMod's
// RandomizerItem; verified in docs/multiworld-state.md.
struct RandomizerEntry {
    int32_t id;  // level number << 16 | object id
    uint8_t unknown_0[0x4];
    std::string vanilla_item;  // filled from the CSV on every level load; not saved
    std::string item;          // the placed item id
    int32_t tracker_x;
    int32_t tracker_y;
    bool revealed;
    bool collected;
    uint8_t unknown_1[0x16];
};
static_assert(sizeof(RandomizerEntry) == 0x68);
static_assert(offsetof(RandomizerEntry, vanilla_item) == 0x08);
static_assert(offsetof(RandomizerEntry, item) == 0x28);
static_assert(offsetof(RandomizerEntry, revealed) == 0x50);
static_assert(offsetof(RandomizerEntry, collected) == 0x51);

// One `onpickup <item>` rule of randomizer_scripts.csv, see item_rules.cpp.
struct ItemRule {
    int32_t target;  // level number << 16 | object id
    uint8_t unknown_0[0x4];
    std::string item;
    std::string action;
};
static_assert(sizeof(ItemRule) == 0x48);

// An action item_rule_action queued for a level that is not loaded.
struct PendingAction {
    int32_t target;
    uint8_t unknown_0[0x4];
    std::string action;
};
static_assert(sizeof(PendingAction) == 0x28);

// The object at globals_offset::randomizer_state, as the loader and mark_collected use it. Only the known part; the
// DLL never creates or copies one.
struct RandomizerState {
    uint8_t unknown_0[0x38];
    std::vector<ItemRule> item_rules;
    uint8_t unknown_1[0x30];
    uint32_t seed;
    int32_t difficulty;
    std::vector<RandomizerEntry> entries;  // globals_offset::randomizer_entries
    std::vector<PendingAction> pending_actions;  // emptied as levels load
};
static_assert(offsetof(RandomizerState, item_rules) == 0x38);
static_assert(offsetof(RandomizerState, seed) == 0x80);
static_assert(offsetof(RandomizerState, difficulty) == 0x84);
static_assert(offsetof(RandomizerState, entries) == 0x88);
static_assert(offsetof(RandomizerState, pending_actions) == 0xa0);
static_assert(globals_offset::randomizer_state + offsetof(RandomizerState, entries) == globals_offset::randomizer_entries);

// One element of globals_offset::inventory, as getItemCount (0x202c10) reads it.
struct InventoryItem {
    float unknown_0;
    float unknown_1;
    std::string item;
    void* image;
    void* unknown_2;
    int32_t count;
    std::string description;
};
static_assert(sizeof(InventoryItem) == 0x60);
static_assert(offsetof(InventoryItem, item) == 0x08);
static_assert(offsetof(InventoryItem, count) == 0x38);

// A field of a game object at a byte offset.
template <class T>
T& field(void* object, uintptr_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// Finds out which build is running and checks that nothing else patched it. Empty on success.
std::string verify();
// The running build; only valid once verify() succeeded.
const Build& build();

uint8_t* base();
uint8_t* globals();  // null until the game has set up its globals

// The game function at a Build address, e.g. game_fn<void (*)(void*)>(&Build::unpause).
template <class Fn>
Fn game_fn(uintptr_t Build::* rva) {
    return reinterpret_cast<Fn>(base() + build().*rva);
}

// A randomizer game is loaded or being started; false before the game has globals.
bool is_randomizer_game();
// Null before the game has globals.
RandomizerState* randomizer_state();

// Empty before the game has globals.
std::string current_level();
// The language the game's texts are loaded in ("en", "de", ...); empty before the game has globals.
std::string text_language();
// False in the main menu, where items must not be given.
bool in_game();
// Counts simulated frames: stands still while paused, loading or in a menu, unlike the tick.
uint32_t frame_counter();

// The game's per-location state; empty before a save is loaded. Survives level changes, so it is always the full
// collected set.
std::span<RandomizerEntry> randomizer_entries();
// level number << 16 | object id of a pickup_item; 0 if it has no level yet.
uint32_t pickup_location(void* pickup);

std::span<const InventoryItem> inventory();
// Same value as the getItemCount native.
int32_t item_count(const std::string& item);

// The save's `quests_completed`; null before the globals exist. Only for markers.cpp; everything else goes through it.
std::vector<std::string>* quests_completed();

void inventory_add(const std::string& item);

struct Ball {
    uint8_t* object;
    uint8_t* level;
    float x;  // level units
    float y;
};
// The ball, if the game has one that can own objects.
std::optional<Ball> ball();

// Later: the physics world is busy; try again on another tick.
enum class LootResult { NotLoot, Given, Later, Failed };
// If `name` is a loot table (fruit), spawns it as a bubble at the ball and lets the ball take it at once, so the game
// hands out the fruit with its own rules. NotLoot for every other item.
LootResult give_loot(const std::string& name);
// The game's runner of one `onpickup` rule (see item_rules.cpp): applies `action` to `target` if that level is
// loaded, else queues it until the level loads. Reads the main map through the level being played.
void item_rule_action(RandomizerState* state, int32_t target, const char* action);
// Takes one copy of `item` out of the inventory. False if none was held.
bool inventory_remove(const std::string& item);
void set_string(const std::string& key, const std::string& value);
void erase_string(const std::string& key);
// The dialog string for key; nullopt if the dialog table has no such key.
std::optional<std::string> get_string(const std::string& key);
bool has_string(const std::string& key);
// Sets `entries` in the dialog table, adding missing keys and overwriting the game's own: every line with the key, where
// the game has alternative lines under one key. Cheap per tick: only rechecks when the table was replaced or resized.
void ensure_dialog_strings(const std::vector<std::pair<std::string, std::string>>& entries);
// Adds a "ui_" key unless present. False while the UI text is not loaded yet.
bool ensure_ui_string(const std::string& key, const std::string& value);
// Adds to `fruit` and `total_fruit`, like addFruit with a positive amount.
void add_fruit(int32_t amount);
bool dialog_open();
void show_dialog(const std::string& title, const std::string& text);

struct Native {
    std::string name;
    uintptr_t rva;
    uintptr_t registration;  // its `lea rdx, [fn]` in script_api_init
};
// The script natives, read from script_api_init's registration code.
std::vector<Native> find_natives();

void* lua_newstate();
void luaL_openlibs(void* state);
void lua_setglobal(void* state, const char* name);
void lua_pushstring(void* state, const char* text);
bool run_script(void* context, const char* source);

}  // namespace oyr::game
