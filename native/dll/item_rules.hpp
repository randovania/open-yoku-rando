#pragma once

// The `onpickup <item>` rules of randomizer_scripts.csv, see item_rules.cpp.
namespace oyr::item_rules {

// Per tick: runs the rules of held items that still need them, only when `running`.
void on_tick(bool running);

}  // namespace oyr::item_rules
