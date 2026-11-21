// P05 NetCostLab - C5's binding between the workload and the gameplay prioritiser: assigns drones, sets focus targets.

#pragma once

#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCLFocusSubsystem.generated.h"

class ANCLDrone;
class UNCLGameplayPrioritizer;

/**
 * Server, C5 only (-NCLPrioritizer=Gameplay with Iris): puts every drone under UNCLGameplayPrioritizer and keeps each
 * drone's focus target equal to the avatar it attacks.
 *  - A drone has an Iris handle only once it has begun replicating (Actor.cpp:4768), so drones are assigned from a
 *    pending list, retried each frame until every one has a handle (02 section 9).
 *  - On every state change (ANCLDrone::OnStateChanged, the same event the director uses) the drone's focus becomes the
 *    net handle of the target slot's avatar, or none.
 * The workload stays unaware of the replication system; this class is the only game code that knows the prioritiser.
 */
UCLASS()
class UNCLFocusSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem / UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem / UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** C5 active in this world (the prioritiser was found). */
	bool IsActive() const { return Prioritizer != nullptr; }
	int32 GetAssignedCount() const { return Assigned.Num(); }
	int32 GetPendingCount() const;
	int32 GetAssignFailures() const;
	/** Boosted priorities since the last call (the director's CSV column), 0 when C5 is not active. */
	int32 ConsumeBoostCount();

protected:
	//~ Begin UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	void AssignPendingDrones();
	void UpdateFocus(const ANCLDrone& Drone);
	void OnDroneStateChanged(const ANCLDrone& Drone, bool bFirstState);

	bool bRequested = false;
	FDelegateHandle StateChangedHandle;

	UPROPERTY(Transient)
	TObjectPtr<UNCLGameplayPrioritizer> Prioritizer;

	uint32 PrioritizerHandle = 0;
	/** Drones under the prioritiser, with their Iris handles. */
	TMap<TWeakObjectPtr<const ANCLDrone>, UE::Net::FNetRefHandle> Assigned;
	int32 DronesSeen = 0;
};
