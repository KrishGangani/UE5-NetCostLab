// P05 NetCostLab - ANCLDrone.

#include "NCLDrone.h"

#include "Components/SceneComponent.h"
#include "NCLNet.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLDrone)

ANCLDrone::ANCLDrone()
{
	// Moved and changed by UNCLWorkloadSubsystem on the server: no tick of its own, on any machine.
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	bReplicates = true;
	SetReplicatingMovement(true);
	// Drones move every server tick (30 Hz), so they may be considered every tick.
	SetNetUpdateFrequency(30.0f);
	SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance));
}

void ANCLDrone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams PushBased;
	PushBased.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, State, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, Ammo, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, Armor, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, WeaponId, PushBased);
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, TeamId, PushBased);

	// Sent with the drone's first update only; it never changes afterwards.
	const FDoRepLifetimeParams InitialOnly{COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased*/ true};
	DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, DroneIndex, InitialOnly);
}

void ANCLDrone::SetDroneIndex(int16 NewIndex)
{
	DroneIndex = NewIndex;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, DroneIndex, this);
}

void ANCLDrone::ApplyStateChange(uint8 NewMode, int8 NewTargetSlot, int16 NewHealth)
{
	++State.Seq;
	State.Mode = NewMode;
	State.TargetSlot = NewTargetSlot;
	State.Health = NewHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, State, this);
}

void ANCLDrone::ApplyLoadoutChange(int32 NewAmmo, int32 NewArmor, uint8 NewWeaponId, uint8 NewTeamId)
{
	Ammo = NewAmmo;
	Armor = NewArmor;
	WeaponId = NewWeaponId;
	TeamId = NewTeamId;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, Ammo, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, Armor, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, WeaponId, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, TeamId, this);
}
