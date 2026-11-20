// P05 NetCostLab - ANCLPlayerAvatar.

#include "NCLPlayerAvatar.h"

#include "Components/SceneComponent.h"
#include "NCLNet.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLPlayerAvatar)

ANCLPlayerAvatar::ANCLPlayerAvatar()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance));
}

void ANCLPlayerAvatar::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	const FDoRepLifetimeParams InitialOnly{COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased*/ true};
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLPlayerAvatar, PlayerSlot, InitialOnly);
}

void ANCLPlayerAvatar::SetPlayerSlot(int8 NewSlot)
{
	PlayerSlot = NewSlot;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLPlayerAvatar, PlayerSlot, this);
}
