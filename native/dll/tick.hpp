#pragma once

// The DLL's per-tick work, called from the tick hook before the game's own tick.
namespace oyr::tick {

// Runs every module's tick in order, on the game thread only.
void on_tick();

// Whether the game simulated a frame within the last few ticks (not paused, loading or in a menu); RL.Running().
bool game_running();

}  // namespace oyr::tick
