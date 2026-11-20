// P05 NetCostLab - network settings shared by every replicated actor of the workload.

#pragma once

#include "CoreMinimal.h"

namespace NCLNet
{
	/** 150 m, the engine default (Actor.cpp:312), stated once so drones, props and avatars cull at the same distance in
	 *  every system (01b #11). */
	inline constexpr float CullDistance = 15000.0f;
}
