// P05 NetCostLab - ANCLProp.

#include "NCLProp.h"

#include "Components/SceneComponent.h"
#include "Misc/Crc.h"
#include "NCLNet.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLProp)

ANCLProp::ANCLProp()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	bReplicates = true;
	// Stationary: no movement replication. A low update rate, as a competent setup would use for rarely changing props.
	SetNetUpdateFrequency(5.0f);
	SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance));
}

void ANCLProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams PushBased;
	PushBased.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLProp, PropState, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLProp, Charges, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLProp, Cooldown, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLProp, OwnerSlot, PushBased);

	// Sent with the prop's first update only; it never changes afterwards.
	const FDoRepLifetimeParams InitialOnly{COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased*/ true};
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLProp, PropIndex, InitialOnly);
}

void ANCLProp::SetPropIndex(int16 NewIndex)
{
	PropIndex = NewIndex;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLProp, PropIndex, this);
}

uint32 ANCLProp::GetStateCrc() const
{
	uint32 Crc = FCrc::TypeCrc32(PropState);
	Crc = FCrc::TypeCrc32(Charges, Crc);
	Crc = FCrc::TypeCrc32(Cooldown, Crc);
	return FCrc::TypeCrc32(OwnerSlot, Crc);
}

void ANCLProp::ApplyToggle(int8 NewOwnerSlot)
{
	PropState = PropState == 0 ? 1 : 0;
	Charges = Charges > 0 ? Charges - 1 : 3;
	Cooldown = PropState == 1 ? 20.0f : 0.0f;
	OwnerSlot = NewOwnerSlot;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLProp, PropState, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLProp, Charges, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLProp, Cooldown, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLProp, OwnerSlot, this);
}
