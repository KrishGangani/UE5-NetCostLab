// P05 NetCostLab - a replicated drone: the workload's main moving, gameplay-carrying actor.

#pragma once

#include "GameFramework/Actor.h"
#include "NCLDrone.generated.h"

/** Gameplay state that changes on a schedule. One struct, so Seq, Mode, TargetSlot and Health always arrive together. */
USTRUCT()
struct FNCLDroneState
{
	GENERATED_BODY()

	/** Increments on every gameplay change, so a client can tell exactly which server change it is seeing (step S3). */
	UPROPERTY()
	uint16 Seq = 0;

	/** 0 = patrol, 1 = attack. */
	UPROPERTY()
	uint8 Mode = 0;

	/** The player slot this drone attacks, or -1. That player's avatar is the drone's focus target (C5, C6). */
	UPROPERTY()
	int8 TargetSlot = -1;

	UPROPERTY()
	int16 Health = 100;
};

/**
 * A drone moves on a seeded loop and changes its gameplay state on a fixed schedule. It has no tick of its own:
 * UNCLWorkloadSubsystem moves it and applies its changes on the server, so every configuration runs the same code.
 *
 * Every replicated property is registered push-based and every change calls MARK_PROPERTY_DIRTY_FROM_NAME. With push
 * model off (C1) those calls do nothing and the server compares the properties instead; the game code is identical.
 * The four loadout fields change rarely on purpose: they are what a push-model server can skip and a polling server
 * must compare (hypothesis H1).
 */
UCLASS()
class ANCLDrone : public AActor
{
	GENERATED_BODY()

public:
	ANCLDrone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** C6 (legacy replication only; Iris and the Replication Graph never call it): with ncl.LegacyFocusPriority on, a
	 *  drone whose target is the viewing player's avatar gets the engine's own view-target priority, NetPriority x Time
	 *  x 4 (ActorReplication.cpp:56-59). Otherwise the engine's rule (Super). */
	virtual float GetNetPriority(const FVector& ViewPos, const FVector& ViewDir, AActor* Viewer, AActor* ViewTarget,
		UActorChannel* InChannel, float Time, bool bLowBandwidth) override;

	/** Server: how often the C6 rule raised a priority since the last call, then reset (the director's CSV column). */
	static int32 ConsumeFocusBoostCount();

	/** Server: the drone's stable index (0..N-1), sent once so clients can name drones in their logs. */
	void SetDroneIndex(int16 NewIndex);

	/** Server: applies one scheduled gameplay change (Seq increments) and marks the state dirty. */
	void ApplyStateChange(uint8 NewMode, int8 NewTargetSlot, int16 NewHealth);

	/** Server: changes the rarely changing loadout fields and marks each one dirty. */
	void ApplyLoadoutChange(int32 NewAmmo, int32 NewArmor, uint8 NewWeaponId, uint8 NewTeamId);

	int16 GetDroneIndex() const { return DroneIndex; }
	const FNCLDroneState& GetState() const { return State; }

	/** CRC of every replicated gameplay field (state and loadout): equal on server and client = the same state (S3). */
	uint32 GetStateCrc() const;

	/** Broadcast when a drone's state changes: on the server when a change is applied, on a client when it arrives
	 *  (bFirstState: the first state of an actor that just appeared, i.e. became relevant). The benchmark director
	 *  listens; the drone measures nothing. */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnStateChanged, const ANCLDrone&, bool /*bFirstState*/);
	static FOnStateChanged OnStateChanged;

private:
	UFUNCTION()
	void OnRep_State();

	UPROPERTY(Replicated)
	int16 DroneIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	FNCLDroneState State;

	UPROPERTY(Replicated)
	int32 Ammo = 30;

	UPROPERTY(Replicated)
	int32 Armor = 50;

	UPROPERTY(Replicated)
	uint8 WeaponId = 0;

	UPROPERTY(Replicated)
	uint8 TeamId = 0;
};
