// P05 NetCostLab - the player controller: makes the replication view server-determined.

#pragma once

#include "GameFramework/PlayerController.h"
#include "NCLPlayerController.generated.h"

/**
 * Legacy, Replication Graph and Iris all ask GetPlayerViewPoint where a connection is looking (NetDriver.cpp:5108-5128,
 * 7703-7716). On a server the engine answers from the camera cache that the client keeps updating
 * (PlayerController.cpp:1001-1031), so the answer would depend on the client. Here the server answers with its own
 * view target (the player's ANCLPlayerAvatar), so every configuration sees exactly the same views.
 */
UCLASS()
class ANCLPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: the player slot of this controller's avatar, replicated to the owning client only. */
	void SetPlayerSlot(int8 NewSlot);

	/** The player slot; on a client, its own (or -1 until it arrives). The client's view target cannot tell it: under
	 *  Iris the view-target RPC can arrive before the avatar exists on the client, and the view target stays unset. */
	int8 GetPlayerSlot() const { return PlayerSlot; }

private:
	UPROPERTY(Replicated)
	int8 PlayerSlot = -1;
};
