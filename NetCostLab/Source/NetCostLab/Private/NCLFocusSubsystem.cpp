// P05 NetCostLab - UNCLFocusSubsystem.

#include "NCLFocusSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NCLDrone.h"
#include "NCLGameplayPrioritizer.h"
#include "NCLLog.h"
#include "NCLPlayerAvatar.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLFocusSubsystem)

namespace NCLFocus
{
	/** The prioritiser definition's name in DefaultEngine.ini ([/Script/IrisCore.NetObjectPrioritizerDefinitions]). */
	const FName PrioritizerName(TEXT("NCLGameplayPrioritizer"));
}

bool UNCLFocusSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UNCLFocusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Mode;
	bRequested = FParse::Value(FCommandLine::Get(), TEXT("NCLPrioritizer="), Mode) && Mode == TEXT("Gameplay");
}

void UNCLFocusSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!bRequested || InWorld.GetNetMode() == NM_Client)
	{
		return;
	}
	UReplicationSystem* System = UE::Net::FReplicationSystemUtil::GetReplicationSystem(&InWorld);
	Prioritizer = System ? Cast<UNCLGameplayPrioritizer>(System->GetPrioritizer(NCLFocus::PrioritizerName)) : nullptr;
	if (!Prioritizer)
	{
		// C5 asked for, but no Iris or no prioritiser definition: a configuration error, never a silent C4.
		UE_LOG(LogNetCostLab, Error, TEXT("NCL focus: -NCLPrioritizer=Gameplay but prioritiser '%s' not found (Iris on?)"),
			*NCLFocus::PrioritizerName.ToString());
		return;
	}
	PrioritizerHandle = System->GetPrioritizerHandle(NCLFocus::PrioritizerName);
	StateChangedHandle = ANCLDrone::OnStateChanged.AddUObject(this, &UNCLFocusSubsystem::OnDroneStateChanged);
	UE_LOG(LogNetCostLab, Display, TEXT("NCL focus: gameplay prioritiser active (handle %u)"), PrioritizerHandle);
}

void UNCLFocusSubsystem::Deinitialize()
{
	ANCLDrone::OnStateChanged.Remove(StateChangedHandle);
	Prioritizer = nullptr;
	Super::Deinitialize();
}

int32 UNCLFocusSubsystem::GetPendingCount() const
{
	return FMath::Max(0, DronesSeen - Assigned.Num());
}

int32 UNCLFocusSubsystem::GetAssignFailures() const
{
	return Prioritizer ? Prioritizer->GetAssignFailures() : 0;
}

int32 UNCLFocusSubsystem::ConsumeBoostCount()
{
	return Prioritizer ? Prioritizer->ConsumeBoostCount() : 0;
}

void UNCLFocusSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Prioritizer && (DronesSeen == 0 || GetPendingCount() > 0))
	{
		AssignPendingDrones();
	}
}

void UNCLFocusSubsystem::AssignPendingDrones()
{
	UReplicationSystem* System = UE::Net::FReplicationSystemUtil::GetReplicationSystem(GetWorld());
	const UObjectReplicationBridge* Bridge = System ? System->GetReplicationBridgeAs<UObjectReplicationBridge>() : nullptr;
	if (!Bridge)
	{
		return;
	}
	DronesSeen = 0;
	for (TActorIterator<ANCLDrone> It(GetWorld()); It; ++It)
	{
		const ANCLDrone* Drone = *It;
		++DronesSeen;
		if (Assigned.Contains(Drone))
		{
			continue;
		}
		// Valid once the drone has begun replicating; until then it stays pending and is retried next frame.
		const UE::Net::FNetRefHandle Handle = Bridge->GetReplicatedRefHandle(Drone);
		if (Handle.IsValid() && Prioritizer->Assign(*System, PrioritizerHandle, Handle))
		{
			Assigned.Add(Drone, Handle);
			UpdateFocus(*Drone);
		}
	}
	if (GetPendingCount() == 0)
	{
		UE_LOG(LogNetCostLab, Display, TEXT("NCL focus: %d drones assigned, %d assign failures"), Assigned.Num(), GetAssignFailures());
	}
}

void UNCLFocusSubsystem::UpdateFocus(const ANCLDrone& Drone)
{
	const UE::Net::FNetRefHandle* Handle = Assigned.Find(&Drone);
	if (!Handle)
	{
		return;     // not yet assigned: AssignPendingDrones sets its focus when it is
	}
	const int8 TargetSlot = Drone.GetState().TargetSlot;
	UE::Net::FNetHandle Target;
	if (TargetSlot >= 0)
	{
		for (TActorIterator<ANCLPlayerAvatar> It(GetWorld()); It; ++It)
		{
			if (It->GetPlayerSlot() == TargetSlot)
			{
				Target = UE::Net::FReplicationSystemUtil::GetNetHandle(*It);
				break;
			}
		}
	}
	Prioritizer->SetFocusTarget(*Handle, Target);
}

void UNCLFocusSubsystem::OnDroneStateChanged(const ANCLDrone& Drone, bool bFirstState)
{
	if (Drone.GetWorld() == GetWorld())
	{
		UpdateFocus(Drone);
	}
}

TStatId UNCLFocusSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNCLFocusSubsystem, STATGROUP_Tickables);
}
