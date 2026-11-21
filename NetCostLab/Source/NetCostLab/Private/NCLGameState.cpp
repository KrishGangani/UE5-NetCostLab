// P05 NetCostLab - ANCLGameState.

#include "NCLGameState.h"

#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLGameState)

void ANCLGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams PushBased;
	PushBased.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLGameState, BenchPhase, PushBased);
}

void ANCLGameState::SetBenchPhase(ENCLBenchPhase NewPhase)
{
	BenchPhase = NewPhase;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLGameState, BenchPhase, this);
	// The phase must reach clients promptly: ask for a net update now instead of waiting for the actor's update interval.
	ForceNetUpdate();
}
