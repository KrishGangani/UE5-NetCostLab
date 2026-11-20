// P05 NetCostLab - ANCLPlayerController.

#include "NCLPlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLPlayerController)

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
