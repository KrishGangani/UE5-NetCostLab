// P05 NetCostLab - the server-driven stand-in for one connected player.

#pragma once

#include "GameFramework/Actor.h"
#include "NCLPlayerAvatar.generated.h"

/**
 * One per connected player. It is not a pawn and nobody possesses it: the server moves it on the player's seeded loop
 * and makes it the player controller's view target, so the view every replication system uses (relevancy, priority)
 * is decided by the server alone and is identical in every configuration (01b #12). Clients send no gameplay input.
 */
UCLASS()
class ANCLPlayerAvatar : public AActor
{
	GENERATED_BODY()

public:
	ANCLPlayerAvatar();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: the player slot this avatar belongs to, sent once. Drones store slots, not pointers (02 section 4.2). */
	void SetPlayerSlot(int8 NewSlot);

	int8 GetPlayerSlot() const { return PlayerSlot; }

private:
	UPROPERTY(Replicated)
	int8 PlayerSlot = -1;
};
