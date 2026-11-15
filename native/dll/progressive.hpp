#pragma once

#include <string>

// Progressive pickups, see progressive.cpp.
namespace oyr::progressive {

// Before pickup_dialog: turns a base stage that is already held into its upgrade and returns the base stage, else
// empty.
std::string upgrade_pickup_stage(void* pickup);
// After pickup_dialog: removes that base stage.
void drop_replaced_stage(const std::string& base);

}  // namespace oyr::progressive
