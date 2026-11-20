// P05 NetCostLab - primary game module registration and the module's log category.

#include "Modules/ModuleManager.h"
#include "NCLLog.h"

DEFINE_LOG_CATEGORY(LogNetCostLab);

// The game module needs no startup logic, so the engine's default implementation is enough.
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, NetCostLab, "NetCostLab");
