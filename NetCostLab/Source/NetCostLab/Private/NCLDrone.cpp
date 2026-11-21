// P05 NetCostLab - ANCLDrone.

#include "NCLDrone.h"

#include "Components/SceneComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "NCLPlayerAvatar.h"
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

ANCLDrone::FOnStateChanged ANCLDrone::OnStateChanged;

namespace NCLDronePriority
{
	/** C6 switch. Server-only, read on every priority call; set with -dpcvars=ncl.LegacyFocusPriority=1. */
	TAutoConsoleVariable<bool> CVarLegacyFocusPriority(TEXT("ncl.LegacyFocusPriority"), false,
		TEXT("C6: a drone attacking the viewer's avatar gets the engine's view-target priority (x4) in legacy replication."));

	/** Boosted priorities since the director last read the count. Game thread only (legacy prioritisation runs there). */
	int32 FocusBoostCount = 0;
}

int32 ANCLDrone::ConsumeFocusBoostCount()
{
	const int32 Count = NCLDronePriority::FocusBoostCount;
	NCLDronePriority::FocusBoostCount = 0;
	return Count;
}

float ANCLDrone::GetNetPriority(const FVector& ViewPos, const FVector& ViewDir, AActor* Viewer, AActor* ViewTarget,
	UActorChannel* InChannel, float Time, bool bLowBandwidth)
{
	if (NCLDronePriority::CVarLegacyFocusPriority.GetValueOnGameThread() && State.TargetSlot >= 0)
	{
		const ANCLPlayerAvatar* Avatar = Cast<ANCLPlayerAvatar>(ViewTarget);
		if (Avatar && Avatar->GetPlayerSlot() == State.TargetSlot)
		{
			// The same factor the engine gives the viewer's own view target (ActorReplication.cpp:56-59).
			++NCLDronePriority::FocusBoostCount;
			return NetPriority * Time * 4.0f;
		}
	}
	return Super::GetNetPriority(ViewPos, ViewDir, Viewer, ViewTarget, InChannel, Time, bLowBandwidth);
}

void ANCLDrone::OnRep_State()
{
	// A newly replicated actor gets its notifies before it begins play (legacy: DataChannel.cpp:3518 then :3537;
	// Iris: the state is applied before PostNetInit, NetActorFactory.cpp:554), so this marks a first state.
	OnStateChanged.Broadcast(*this, !HasActorBegunPlay());
}

uint32 ANCLDrone::GetStateCrc() const
{
	// Field by field, never the struct's bytes, so padding can never enter the CRC.
	uint32 Crc = FCrc::TypeCrc32(State.Seq);
	Crc = FCrc::TypeCrc32(State.Mode, Crc);
	Crc = FCrc::TypeCrc32(State.TargetSlot, Crc);
	Crc = FCrc::TypeCrc32(State.Health, Crc);
	Crc = FCrc::TypeCrc32(Ammo, Crc);
	Crc = FCrc::TypeCrc32(Armor, Crc);
	Crc = FCrc::TypeCrc32(WeaponId, Crc);
	return FCrc::TypeCrc32(TeamId, Crc);
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
	OnStateChanged.Broadcast(*this, false);
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
