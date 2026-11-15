#include "new_game.hpp"

#include "game.hpp"
#include "log.hpp"
#include "markers.hpp"
#include "text.hpp"
#include "warp.hpp"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oyr::new_game {
namespace {

namespace fs = std::filesystem;

constexpr int32_t kDifficultyHard = 2;  // the logic open-yoku-rando's seeds are generated for
constexpr int kSeedFormat = 1;

struct Seed {
    std::string identifier;
    std::string uuid;   // Randovania's layout UUID; optional
    std::string hash;   // what Randovania shows as the seed hash; optional
    uint32_t seed = 0;  // the schema allows the full 32 bits; the game keeps it in an int32
    int32_t fruit = 0;
    std::vector<std::pair<std::string, int32_t>> starting_items;
    std::map<uint32_t, std::string> placement;  // location id -> game item id
    warp::Location start_location;
    SeedInfo info;
};

// The seed file as prepare_menu read it; empty without a valid one. Never changes afterwards.
std::optional<Seed> g_seed;
// g_seed's info, for seed_info().
std::optional<SeedInfo> g_seed_info;

fs::path seed_path() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path() / L"open-yoku-rando" / L"seed.txt";
}

using text::parse_number;

// The patcher's seed file: UTF-8, one tab-separated record per line, '#' starts a comment line.
//   format      1
//   identifier  <Randovania's configuration identifier>
//   uuid        <Randovania's layout UUID>  (optional, for multiworld)
//   hash        <the seed hash as Randovania shows it>  (optional, for the menu)
//   seed        <number shown as the save's seed>
//   fruit       <starting fruit>
//   start       <item>  <count>        (any number of lines)
//   start_location  <level>  <spawner>  (where a new game begins)
//   place       <location id>  <item>  (one line per location)
//   rdv_version    <verison string>
//   oyr_version    <version string>
// Returns an error message, empty on success.
std::string read_seed(const fs::path& path, Seed& seed) {
    std::optional<std::vector<std::string>> lines = text::read_lines(path);
    if (!lines) {
        return std::format("there is no seed file at {}", path.string());
    }
    int format = 0;
    for (size_t index = 0; index < lines->size(); ++index) {
        const std::string& line = (*lines)[index];
        const size_t number = index + 1;
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        std::vector<std::string_view> f = text::split(line, '\t');
        bool ok = false;
        if (f[0] == "format" && f.size() == 2) {
            ok = parse_number(f[1], format);
        } else if (f[0] == "identifier" && f.size() == 2) {
            seed.identifier = f[1];
            ok = !seed.identifier.empty();
        } else if (f[0] == "uuid" && f.size() == 2) {
            seed.uuid = f[1];
            ok = !seed.uuid.empty();
        } else if (f[0] == "hash" && f.size() == 2) {
            seed.hash = f[1];
            ok = !seed.hash.empty();
        } else if (f[0] == "seed" && f.size() == 2) {
            ok = parse_number(f[1], seed.seed);
        } else if (f[0] == "fruit" && f.size() == 2) {
            ok = parse_number(f[1], seed.fruit);
        } else if (f[0] == "start" && f.size() == 3) {
            int32_t count = 0;
            ok = !f[1].empty() && parse_number(f[2], count) && count > 0;
            seed.starting_items.emplace_back(std::string(f[1]), count);
        } else if (f[0] == "start_location" && f.size() == 3) {
            seed.start_location.level = f[1];
            ok = !seed.start_location.level.empty() && parse_number(f[2], seed.start_location.spawner);
        } else if (f[0] == "place" && f.size() == 3) {
            uint32_t location = 0;
            ok = parse_number(f[1], location) && !f[2].empty() && seed.placement.emplace(location, std::string(f[2])).second;
        } else if (f[0] == "rdv_version" && f.size() == 2) {
            seed.info.rdv_version = f[1];
            ok = !seed.info.rdv_version.empty();
        } else if (f[0] == "oyr_version" && f.size() == 2) {
            seed.info.oyr_version = f[1];
            ok = !seed.info.oyr_version.empty();
        }
        if (!ok) {
            return std::format("{} line {} is not understood: {}", path.string(), number, line);
        }
    }
    if (format != kSeedFormat) {
        return std::format("{} has format {}, the DLL reads format {}", path.string(), format, kSeedFormat);
    }
    if (seed.identifier.empty() || seed.start_location.level.empty() || seed.placement.empty()) {
        return std::format("{} has no identifier, start location or placement", path.string());
    }
    return {};
}

std::string g_menu_text;

// Seed number -> hash of every seed started here, kept in seed-history.txt ("<seed>\t<hash>" lines) because a slot
// outlives its seed file.
std::map<uint32_t, std::string> g_hash_by_seed;

fs::path history_path() {
    return seed_path().parent_path() / L"seed-history.txt";
}

std::vector<std::pair<std::string, std::string>> g_texts;

void load_texts() {
    fs::path path = seed_path().parent_path() / L"texts.strings";
    for (std::string& line : text::read_lines(path).value_or(std::vector<std::string>())) {
        // A UTF-8 byte order mark, as the game's own .strings files start with.
        if (line.size() >= 3 && static_cast<uint8_t>(line[0]) == 0xef && static_cast<uint8_t>(line[1]) == 0xbb && static_cast<uint8_t>(line[2]) == 0xbf) {
            line.erase(0, 3);
        }
        size_t separator = line.find(';');
        if (separator != std::string::npos && separator > 0) {
            g_texts.emplace_back(line.substr(0, separator), line.substr(separator + 1));
        }
    }
    log::info("{} texts in {}", g_texts.size(), path.string());
}

std::string shown_hash(const Seed& seed) {
    return seed.hash.empty() ? seed.identifier : seed.hash;
}

void load_history() {
    for (const std::string& line : text::read_lines(history_path()).value_or(std::vector<std::string>())) {
        std::vector<std::string_view> f = text::split(line, '\t');
        uint32_t number = 0;
        if (f.size() == 2 && parse_number(f[0], number) && !f[1].empty()) {
            g_hash_by_seed[number] = f[1];
        }
    }
}

void remember_seed(uint32_t number, const std::string& hash) {
    auto [entry, added] = g_hash_by_seed.try_emplace(number, hash);
    if (!added && entry->second == hash) {
        return;
    }
    entry->second = hash;
    std::ofstream(history_path(), std::ios::binary | std::ios::app) << number << '\t' << hash << '\n';
}

}  // namespace

const std::string* hash_for_seed(uint32_t seed) {
    auto entry = g_hash_by_seed.find(seed);
    return entry != g_hash_by_seed.end() ? &entry->second : nullptr;
}

const std::vector<std::pair<std::string, std::string>>& texts() {
    return g_texts;
}

const std::optional<SeedInfo>& seed_info() {
    return g_seed_info;
}

MenuEntry prepare_menu() {
    load_history();
    load_texts();
    fs::path path = seed_path();
    Seed seed;
    MenuEntry entry;
    if (!fs::exists(path)) {
        entry.text = "No Randovania seed found";
        log::info("menu: there is no seed file at {}", path.string());
    } else if (std::string error = read_seed(path, seed); !error.empty()) {
        entry.text = "Randovania seed file is invalid";
        log::info("menu: {}", error);
    } else {
        entry.text = std::format("Start Randovania Seed: {}", shown_hash(seed));
        entry.startable = true;
        remember_seed(seed.seed, shown_hash(seed));
        log::info("menu: seed {} is ready", seed.identifier);
        g_seed_info = seed.info;
        g_seed = std::move(seed);
    }
    g_menu_text = entry.text;
    return entry;
}

const std::string& menu_text() {
    return g_menu_text;
}

const char* first_level(const char* level) {
    if (!level || !g_seed || !game::is_randomizer_game() || !game::randomizer_entries().empty()) {
        return level;
    }
    const warp::Location& start = g_seed->start_location;
    uint8_t* globals = game::globals();
    game::field<std::string>(globals, game::globals_offset::level) = start.level;
    game::field<int32_t>(globals, game::globals_offset::spawner) = start.spawner;
    log::info("new game of seed {} starts at {} spawner {} instead of {}", g_seed->identifier, start.level, start.spawner, level);
    return start.level.c_str();
}

bool before_loader(game::RandomizerState* state) {
    if (!game::is_randomizer_game()) {
        return false;
    }
    auto& entries = state->entries;
    if (!entries.empty()) {
        return false;  // a loaded save, or a level after the first: the loader does not roll either
    }
    if (!g_seed) {
        log::info("new randomizer game without a usable Randovania seed, the game rolls its own");
        return false;
    }
    state->difficulty = kDifficultyHard;
    state->seed = g_seed->seed;
    // The loader only rolls into an empty entry vector. With a placeholder it treats the game like a loaded save:
    // fills every entry from the CSV but keeps the items, which after_loader then sets.
    entries.emplace_back();
    return true;
}

void after_loader(game::RandomizerState* state, bool new_game) {
    if (!new_game || !g_seed) {
        return;
    }
    const Seed& seed = *g_seed;

    auto& entries = state->entries;
    size_t placed = 0;
    for (game::RandomizerEntry& entry : entries) {
        auto item = seed.placement.find(static_cast<uint32_t>(entry.id));
        if (item == seed.placement.end()) {
            log::info("the seed places nothing at location {} ({}); it keeps an empty item", entry.id, entry.vanilla_item);
            continue;
        }
        entry.item = item->second;
        ++placed;
    }
    if (placed != seed.placement.size()) {
        log::info("the seed names {} locations the game does not have", seed.placement.size() - placed);
    }

    for (const auto& [item, count] : seed.starting_items) {
        for (int32_t i = 0; i < count; ++i) {
            game::inventory_add(item);
        }
    }
    if (seed.fruit > 0) {
        game::add_fruit(seed.fruit);
    }
    if (markers::mark_save(seed.identifier)) {
        if (!seed.uuid.empty()) {
            markers::add(std::string(markers::kLayoutUuid) + seed.uuid);
        }
        markers::add(warp::marker(seed.start_location));
    }
    remember_seed(seed.seed, shown_hash(seed));
    log::info("started Randovania seed {} (seed {}) on thread {}: {} of {} locations placed, {} starting items, {} fruit", seed.identifier, seed.seed,
              GetCurrentThreadId(), placed, entries.size(), seed.starting_items.size(), seed.fruit);
}

}  // namespace oyr::new_game
