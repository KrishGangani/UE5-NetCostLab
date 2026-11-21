// P05 NetCostLab - the gameplay-aware Iris prioritiser: Epic's sphere (distance) model plus a focus boost.

#pragma once

#include "Containers/ArrayView.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/Prioritization/SphereNetObjectPrioritizer.h"
#include "Net/Core/NetHandle/NetHandle.h"
#include "NCLGameplayPrioritizer.generated.h"

class UReplicationSystem;

/** The prioritiser's settings, read from [/Script/NetCostLabCore.NCLGameplayPrioritizerConfig] (and the sphere's own). */
UCLASS(Transient, Config = Engine)
class NETCOSTLABCORE_API UNCLGameplayPrioritizerConfig : public USphereNetObjectPrioritizerConfig
{
	GENERATED_BODY()

public:
	/** Added to the distance priority of an object whose focus target is the connection's view target or controller. 2.0
	 *  lifts even an object outside the sphere (0.1) above 1.0, the level at which Iris considers an object (02 section 2). */
	UPROPERTY(Config)
	float FocusPriorityBoost = 2.0f;
};

/** One connection view, reduced to what the focus rule compares: the ids of its view target and controller. */
struct FNCLFocusView
{
	uint64 ViewTargetId = 0;
	uint64 ControllerId = 0;
};

/**
 * priority(object, connection) = sphere(distance(object, view))                    (Epic's model, unchanged: Super)
 *                              + FocusPriorityBoost  if focus(object) == the connection's view target or controller
 *
 * The game says which object focuses on what: Assign() puts an object under this prioritiser and learns its internal
 * index; SetFocusTarget() names the actor it is about (a drone's target avatar), by its Iris net handle. Prioritize()
 * runs Super::Prioritize and then ApplyFocusBoost on the same batch.
 *
 * Identity, without private Iris headers: Iris passes prioritisers only an internal object index. SetPrioritizer calls
 * AddObject synchronously (ReplicationSystem.cpp, 5.8.4), so Assign() captures the index AddObject is called with.
 * An object whose index could not be captured is never boosted (counted in AssignFailures).
 */
UCLASS(Transient)
class NETCOSTLABCORE_API UNCLGameplayPrioritizer : public USphereNetObjectPrioritizer
{
	GENERATED_BODY()

public:
	/** Server: puts the object under prioritiser Self (from GetPrioritizerHandle with this prioritiser's definition name)
	 *  and learns its index. False if Iris refused the object or the index could not be captured. */
	bool Assign(UReplicationSystem& System, UE::Net::FNetObjectPrioritizerHandle Self, UE::Net::FNetRefHandle Object);

	/** Server: the object's focus target, e.g. the avatar a drone attacks. False if the object was not assigned. */
	bool SetFocusTarget(UE::Net::FNetRefHandle Object, UE::Net::FNetHandle Target);
	bool ClearFocusTarget(UE::Net::FNetRefHandle Object);

	/** The focus rule on one batch, pure so it can be tested with plain arrays: adds Boost to the priority of every
	 *  object whose focus id (0 = none) equals a view's target or controller id. Returns how many it boosted. */
	static int32 ApplyFocusBoost(TConstArrayView<uint32> ObjectIndices, TArrayView<float> Priorities,
		TConstArrayView<uint64> FocusIdByIndex, TConstArrayView<FNCLFocusView> Views, float Boost);

	int32 GetAssignFailures() const { return AssignFailures; }
	/** Boosted priorities since the last call, then reset (the director's CSV column). */
	int32 ConsumeBoostCount();

protected:
	//~ Begin UNetObjectPrioritizer
	virtual void Init(FNetObjectPrioritizerInitParams& Params) override;
	virtual void Deinit() override;
	virtual void OnMaxInternalNetRefIndexIncreased(UE::Net::FInternalNetRefIndex NewMaxInternalIndex) override;
	virtual bool AddObject(UE::Net::FInternalNetRefIndex ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params) override;
	virtual void RemoveObject(UE::Net::FInternalNetRefIndex ObjectIndex, const FNetObjectPrioritizationInfo& Info) override;
	virtual void Prioritize(FNetObjectPrioritizationParams& Params) override;
	//~ End UNetObjectPrioritizer

private:
	static constexpr uint32 InvalidIndex = ~0u;

	float FocusPriorityBoost = 2.0f;
	/** Per internal object index: the net handle id of its focus target (0 = none). */
	TArray<uint64> FocusIdByIndex;
	TMap<UE::Net::FNetRefHandle, uint32> IndexByHandle;

	bool bCapturing = false;
	uint32 CapturedIndex = InvalidIndex;
	int32 AssignFailures = 0;
	int32 BoostCount = 0;
};
