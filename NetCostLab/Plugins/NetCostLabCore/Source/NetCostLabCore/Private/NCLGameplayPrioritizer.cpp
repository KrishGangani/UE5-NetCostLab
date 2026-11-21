// P05 NetCostLab - UNCLGameplayPrioritizer.

#include "NCLGameplayPrioritizer.h"

#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLGameplayPrioritizer)

int32 UNCLGameplayPrioritizer::ApplyFocusBoost(TConstArrayView<uint32> ObjectIndices, TArrayView<float> Priorities,
	TConstArrayView<uint64> FocusIdByIndex, TConstArrayView<FNCLFocusView> Views, float Boost)
{
	int32 Boosted = 0;
	for (const uint32 ObjectIndex : ObjectIndices)
	{
		const uint64 FocusId = FocusIdByIndex.IsValidIndex(ObjectIndex) ? FocusIdByIndex[ObjectIndex] : 0;
		if (FocusId == 0)
		{
			continue;
		}
		for (const FNCLFocusView& View : Views)
		{
			// One boost per object, however many of the connection's views match (split screen).
			if (View.ViewTargetId == FocusId || View.ControllerId == FocusId)
			{
				Priorities[ObjectIndex] += Boost;
				++Boosted;
				break;
			}
		}
	}
	return Boosted;
}

void UNCLGameplayPrioritizer::Init(FNetObjectPrioritizerInitParams& Params)
{
	Super::Init(Params);
	FocusPriorityBoost = CastChecked<UNCLGameplayPrioritizerConfig>(Params.Config)->FocusPriorityBoost;
	FocusIdByIndex.SetNumZeroed(Params.CurrentMaxInternalIndex);
}

void UNCLGameplayPrioritizer::Deinit()
{
	FocusIdByIndex.Empty();
	IndexByHandle.Empty();
	Super::Deinit();
}

void UNCLGameplayPrioritizer::OnMaxInternalNetRefIndexIncreased(UE::Net::FInternalNetRefIndex NewMaxInternalIndex)
{
	Super::OnMaxInternalNetRefIndexIncreased(NewMaxInternalIndex);
	FocusIdByIndex.SetNumZeroed(NewMaxInternalIndex);
}

bool UNCLGameplayPrioritizer::AddObject(UE::Net::FInternalNetRefIndex ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params)
{
	const bool bAdded = Super::AddObject(ObjectIndex, Params);
	if (bAdded && bCapturing)
	{
		CapturedIndex = ObjectIndex;
	}
	return bAdded;
}

void UNCLGameplayPrioritizer::RemoveObject(UE::Net::FInternalNetRefIndex ObjectIndex, const FNetObjectPrioritizationInfo& Info)
{
	if (FocusIdByIndex.IsValidIndex(ObjectIndex))
	{
		FocusIdByIndex[ObjectIndex] = 0;
	}
	for (auto It = IndexByHandle.CreateIterator(); It; ++It)
	{
		if (It.Value() == ObjectIndex)
		{
			It.RemoveCurrent();
			break;
		}
	}
	Super::RemoveObject(ObjectIndex, Info);
}

bool UNCLGameplayPrioritizer::Assign(UReplicationSystem& System, UE::Net::FNetObjectPrioritizerHandle Self, UE::Net::FNetRefHandle Object)
{
	// SetPrioritizer calls AddObject inside the call (ReplicationPrioritization.cpp:414, 452), so the index AddObject
	// sees is this object's. Never boost on a guess: a missing index is a failure.
	CapturedIndex = InvalidIndex;
	bCapturing = true;
	const bool bSet = System.SetPrioritizer(Object, Self);
	bCapturing = false;
	if (!bSet || CapturedIndex == InvalidIndex || !FocusIdByIndex.IsValidIndex(CapturedIndex))
	{
		++AssignFailures;
		return false;
	}
	IndexByHandle.Add(Object, CapturedIndex);
	return true;
}

bool UNCLGameplayPrioritizer::SetFocusTarget(UE::Net::FNetRefHandle Object, UE::Net::FNetHandle Target)
{
	const uint32* Index = IndexByHandle.Find(Object);
	if (!Index)
	{
		return false;
	}
	FocusIdByIndex[*Index] = Target.IsValid() ? Target.GetId() : 0;
	return true;
}

bool UNCLGameplayPrioritizer::ClearFocusTarget(UE::Net::FNetRefHandle Object)
{
	return SetFocusTarget(Object, UE::Net::FNetHandle());
}

int32 UNCLGameplayPrioritizer::ConsumeBoostCount()
{
	const int32 Count = BoostCount;
	BoostCount = 0;
	return Count;
}

void UNCLGameplayPrioritizer::Prioritize(FNetObjectPrioritizationParams& Params)
{
	// Epic's distance model first, unchanged: C5 = C4 + one term.
	Super::Prioritize(Params);

	TArray<FNCLFocusView, TInlineAllocator<4>> Views;
	for (const UE::Net::FReplicationView::FView& View : Params.View.Views)
	{
		Views.Add({View.ViewTarget.IsValid() ? View.ViewTarget.GetId() : 0, View.Controller.IsValid() ? View.Controller.GetId() : 0});
	}
	// Priorities is indexed by internal object index (NetObjectPrioritizer.h: "Index using ObjectIndices[...]").
	BoostCount += ApplyFocusBoost(MakeArrayView(Params.ObjectIndices, Params.ObjectCount),
		MakeArrayView(Params.Priorities, FocusIdByIndex.Num()), FocusIdByIndex, Views, FocusPriorityBoost);
}
