#pragma once

#include "game/ghosts/ghost_world.h"

namespace ghost::game {
void tickVasskraka(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access);

}
