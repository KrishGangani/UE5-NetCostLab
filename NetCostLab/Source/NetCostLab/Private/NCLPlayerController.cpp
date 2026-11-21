// P05 NetCostLab - ANCLPlayerController.

#include "NCLPlayerController.h"

#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLPlayerController)

void ANCLPlayerController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// To the owning client only: no other connection needs it.
	FDoRepLifetimeParams OwnerOnly;
	OwnerOnly.Condition = COND_OwnerOnly;
	OwnerOnly.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLPlayerController, PlayerSlot, OwnerOnly);
}

void ANCLPlayerController::SetPlayerSlot(int8 NewSlot)
{
	PlayerSlot = NewSlot;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLPlayerController, PlayerSlot, this);
}

void ANCLPlayerController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (HasAuthority())
	{
		if (const AActor* Target = GetViewTarget(); Target && Target != this)
		{
			OutLocation = Target->GetActorLocation();
			OutRotation = Target->GetActorRotation();
			return;
		}
	}
	Super::GetPlayerViewPoint(OutLocation, OutRotation);
}
