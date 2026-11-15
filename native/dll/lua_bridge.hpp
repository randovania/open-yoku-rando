#pragma once

// The DLL's own lua_State, on which Randovania's remote Lua requests run with the RL functions.
namespace oyr::bridge {

// Called from the tick on the game thread: runs one queued remote Lua request, if there is one.
void run_remote_lua();

using NativeFn = int (*)(void* L);
// The game's unlockAchievement native wrapped so goal::on_achievement sees every name first.
NativeFn wrap_unlock_achievement(NativeFn original);

}  // namespace oyr::bridge
