// P05 NetCostLab - the game mode: players get a server-driven avatar instead of a pawn.

#pragma once

#include "GameFramework/GameModeBase.h"
#include "NCLGameMode.generated.h"

UCLASS()
class ANCLGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ANCLGameMode();

	/** Gives the new player an avatar (through the workload subsystem) and makes it the player's view target. */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/** Removes the leaving player's avatar. */
	virtual void Logout(AController* Exiting) override;

	/** Players have no pawn: the workload drives an avatar for each of them, so there is nothing to (re)spawn. */
	virtual void RestartPlayer(AController* /*NewPlayer*/) override {}
};
