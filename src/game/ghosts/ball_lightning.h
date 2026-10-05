#pragma once

#include "game/ghosts/ghost_world.h"

namespace ghost::game {
void tickBallLightning(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access);

}
