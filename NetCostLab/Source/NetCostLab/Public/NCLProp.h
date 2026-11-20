// P05 NetCostLab - a replicated, stationary interactive prop (a door, a pickup): many actors, rare changes.

#pragma once

#include "GameFramework/Actor.h"
#include "NCLProp.generated.h"

/**
 * Props never move. Their four fields toggle on a seeded schedule (about once per 20 s each), so most of the time a
 * polling server compares them for nothing and a push-model server can skip them (hypothesis H1).
 */
UCLASS()
class ANCLProp : public AActor
{
	GENERATED_BODY()

public:
	ANCLProp();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: one scheduled change of all four fields, each marked dirty. */
	void ApplyToggle(int8 NewOwnerSlot);

private:
	UPROPERTY(Replicated)
	uint8 PropState = 0;

	UPROPERTY(Replicated)
	int32 Charges = 3;

	UPROPERTY(Replicated)
	float Cooldown = 0.0f;

	UPROPERTY(Replicated)
	int8 OwnerSlot = -1;
};
