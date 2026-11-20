// P05 NetCostLab - UNCLWorkloadSubsystem.

#include "NCLWorkloadSubsystem.h"

#include "Algo/Count.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/Parse.h"
#include "NCLDrone.h"
#include "NCLLog.h"
#include "NCLPlayerAvatar.h"
#include "NCLProp.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLWorkloadSubsystem)

namespace NCLWorkload
{
	/** Drone loops: 5-30 m radius, 4-6 m/s, centres 2-4 m above the floor. */
	constexpr double DroneMinRadius = 500.0;
	constexpr double DroneMaxRadius = 3000.0;
	constexpr double DroneMinSpeed = 400.0;
	constexpr double DroneMaxSpeed = 600.0;
	constexpr double DroneMinHeight = 200.0;
	constexpr double DroneMaxHeight = 400.0;

	/** Every drone changes its gameplay state every 2 s; drones are spread evenly over the period, so about
	 *  NumDrones / 60 drones change per 30 Hz tick. A quarter of the changes start an attack on a player. */
	constexpr double DroneChangePeriod = 2.0;
	constexpr float AttackProbability = 0.25f;
	constexpr int32 MaxHealthLoss = 10;
	constexpr int16 FullHealth = 100;

	/** Loadout fields change every 30 s per drone: rare on purpose (H1). */
	constexpr double LoadoutChangePeriod = 30.0;

	/** Props sit on a jittered grid near the floor and toggle every 20 s each. */
	constexpr double PropJitter = 150.0;
	constexpr double PropHeight = 50.0;
	constexpr double PropTogglePeriod = 20.0;

	/** Player avatars: 20-80 m loops at 3 m/s, 1.8 m up (eye height). */
	constexpr double AvatarMinRadius = 2000.0;
	constexpr double AvatarMaxRadius = 8000.0;
	constexpr double AvatarSpeed = 300.0;
	constexpr double AvatarHeight = 180.0;
}

bool UNCLWorkloadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UNCLWorkloadSubsystem::FSettings UNCLWorkloadSubsystem::ReadSettings()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FSettings Read;
	FParse::Value(CommandLine, TEXT("NCLSeed="), Read.Seed);
	FParse::Value(CommandLine, TEXT("NCLDrones="), Read.NumDrones);
	FParse::Value(CommandLine, TEXT("NCLProps="), Read.NumProps);
	FParse::Value(CommandLine, TEXT("NCLCountLog="), Read.CountLogInterval);
	Read.NumDrones = FMath::Clamp(Read.NumDrones, 0, static_cast<int32>(MAX_int16));
	Read.NumProps = FMath::Max(Read.NumProps, 0);
	return Read;
}

FRandomStream UNCLWorkloadSubsystem::MakeStream(int32 Seed, EStream Kind, int32 Index, int32 Number)
{
	// A CRC chain mixes the four values. Unsigned arithmetic only, so no seed can overflow a signed integer.
	uint32 Key = FCrc::TypeCrc32(Seed);
	Key = FCrc::TypeCrc32(static_cast<uint8>(Kind), Key);
	Key = FCrc::TypeCrc32(Index, Key);
	Key = FCrc::TypeCrc32(Number, Key);
	return FRandomStream(static_cast<int32>(Key));
}

FVector UNCLWorkloadSubsystem::LoopPosition(const FLoop& Loop, double Time)
{
	const double Angle = Loop.Phase + Loop.AngularSpeed * Time;
	return Loop.Center + FVector(Loop.Radius * FMath::Cos(Angle), Loop.Radius * FMath::Sin(Angle), 0.0);
}

FRotator UNCLWorkloadSubsystem::LoopFacing(const FLoop& Loop, double Time)
{
	// The derivative of LoopPosition: the velocity points along the tangent, its sign set by the direction of travel.
	const double Angle = Loop.Phase + Loop.AngularSpeed * Time;
	return FVector(-Loop.AngularSpeed * FMath::Sin(Angle), Loop.AngularSpeed * FMath::Cos(Angle), 0.0).ToOrientationRotator();
}

int32 UNCLWorkloadSubsystem::TakeDue(FSchedule& Schedule, double Now, double Period)
{
	if (Now < Schedule.NextTime)
	{
		return INDEX_NONE;
	}
	// The next time is a period after the due time, not after now: the schedule keeps its phase.
	Schedule.NextTime += Period;
	return Schedule.Count++;
}

uint32 UNCLWorkloadSubsystem::HashLoop(const FLoop& Loop, uint32 Hash)
{
	Hash = FCrc::TypeCrc32(Loop.Center, Hash);
	Hash = FCrc::TypeCrc32(Loop.Radius, Hash);
	Hash = FCrc::TypeCrc32(Loop.AngularSpeed, Hash);
	return FCrc::TypeCrc32(Loop.Phase, Hash);
}

void UNCLWorkloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Settings = ReadSettings();
}

void UNCLWorkloadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (InWorld.GetNetMode() != NM_Client)
	{
		SpawnWorkload(InWorld);
	}
}

UNCLWorkloadSubsystem::FPlan UNCLWorkloadSubsystem::MakePlan(const FSettings& InSettings)
{
	using namespace NCLWorkload;

	FPlan Plan;
	FRandomStream Placement(InSettings.Seed);
	Plan.Hash = FCrc::TypeCrc32(InSettings.Seed);
	Plan.Hash = FCrc::TypeCrc32(InSettings.NumDrones, Plan.Hash);
	Plan.Hash = FCrc::TypeCrc32(InSettings.NumProps, Plan.Hash);

	Plan.DroneLoops.Reserve(InSettings.NumDrones);
	for (int32 Index = 0; Index < InSettings.NumDrones; ++Index)
	{
		FLoop Loop;
		Loop.Radius = Placement.FRandRange(DroneMinRadius, DroneMaxRadius);
		const double Speed = Placement.FRandRange(DroneMinSpeed, DroneMaxSpeed);
		const double Direction = Placement.FRand() < 0.5f ? -1.0 : 1.0;
		Loop.AngularSpeed = Direction * Speed / Loop.Radius;
		Loop.Phase = Placement.FRandRange(0.0, UE_DOUBLE_TWO_PI);
		const double CentreLimit = ArenaHalfExtent - Loop.Radius;
		Loop.Center = FVector(Placement.FRandRange(-CentreLimit, CentreLimit), Placement.FRandRange(-CentreLimit, CentreLimit),
			Placement.FRandRange(DroneMinHeight, DroneMaxHeight));
		Plan.Hash = HashLoop(Loop, Plan.Hash);
		Plan.DroneLoops.Add(Loop);
	}

	const int32 GridColumns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<double>(InSettings.NumProps))));
	const double GridSpacing = 2.0 * ArenaHalfExtent / GridColumns;
	Plan.PropPositions.Reserve(InSettings.NumProps);
	for (int32 Index = 0; Index < InSettings.NumProps; ++Index)
	{
		const FVector Position(-ArenaHalfExtent + (Index % GridColumns + 0.5) * GridSpacing + Placement.FRandRange(-PropJitter, PropJitter),
			-ArenaHalfExtent + (Index / GridColumns + 0.5) * GridSpacing + Placement.FRandRange(-PropJitter, PropJitter), PropHeight);
		Plan.Hash = FCrc::TypeCrc32(Position, Plan.Hash);
		Plan.PropPositions.Add(Position);
	}
	return Plan;
}

void UNCLWorkloadSubsystem::SpawnWorkload(UWorld& World)
{
	using namespace NCLWorkload;

	const FPlan Plan = MakePlan(Settings);
	const double Now = World.GetTimeSeconds();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	Drones.Reserve(Plan.DroneLoops.Num());
	for (int32 Index = 0; Index < Plan.DroneLoops.Num(); ++Index)
	{
		const FLoop& Loop = Plan.DroneLoops[Index];
		ANCLDrone* Drone = World.SpawnActor<ANCLDrone>(ANCLDrone::StaticClass(), FTransform(LoopPosition(Loop, Now)), SpawnParams);
		check(Drone);
		Drone->SetDroneIndex(static_cast<int16>(Index));
		Drones.Add(Drone);
		// Spread the drones evenly over each period, so the per-tick change count stays flat.
		const double Offset = (Index + 0.5) / Plan.DroneLoops.Num();
		StateSchedules.Add({Now + DroneChangePeriod * Offset, 0});
		LoadoutSchedules.Add({Now + LoadoutChangePeriod * Offset, 0});
	}
	DroneLoops = Plan.DroneLoops;

	Props.Reserve(Plan.PropPositions.Num());
	for (int32 Index = 0; Index < Plan.PropPositions.Num(); ++Index)
	{
		ANCLProp* Prop = World.SpawnActor<ANCLProp>(ANCLProp::StaticClass(), FTransform(Plan.PropPositions[Index]), SpawnParams);
		check(Prop);
		Props.Add(Prop);
		ToggleSchedules.Add({Now + PropTogglePeriod * (Index + 0.5) / Plan.PropPositions.Num(), 0});
	}

	WorkloadHash = Plan.Hash;
	UE_LOG(LogNetCostLab, Display, TEXT("NCL workload: seed=%d drones=%d props=%d hash=0x%08X"),
		Settings.Seed, Settings.NumDrones, Settings.NumProps, WorkloadHash);
}

UNCLWorkloadSubsystem::FLoop UNCLWorkloadSubsystem::MakeAvatarLoop(int32 Seed, int32 Slot)
{
	using namespace NCLWorkload;

	// Each slot has its own stream, so player 7's path is the same however many players there are.
	const FRandomStream Stream = MakeStream(Seed, EStream::AvatarLoop, Slot, 0);
	FLoop Loop;
	Loop.Radius = Stream.FRandRange(AvatarMinRadius, AvatarMaxRadius);
	Loop.AngularSpeed = AvatarSpeed / Loop.Radius;
	Loop.Phase = Stream.FRandRange(0.0, UE_DOUBLE_TWO_PI);
	const double CentreLimit = ArenaHalfExtent - Loop.Radius;
	Loop.Center = FVector(Stream.FRandRange(-CentreLimit, CentreLimit), Stream.FRandRange(-CentreLimit, CentreLimit), AvatarHeight);
	return Loop;
}

ANCLPlayerAvatar* UNCLWorkloadSubsystem::AddPlayer(APlayerController* Player)
{
	UWorld* World = GetWorld();
	if (!Player || !World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	int32 Slot = Avatars.IndexOfByPredicate([](const TObjectPtr<ANCLPlayerAvatar>& Avatar) { return !IsValid(Avatar); });
	if (Slot == INDEX_NONE)
	{
		if (Avatars.Num() > MAX_int8)
		{
			UE_LOG(LogNetCostLab, Error, TEXT("NCL: no free player slot (the maximum is %d players)"), MAX_int8 + 1);
			return nullptr;
		}
		Slot = Avatars.Add(nullptr);
		AvatarOwners.AddDefaulted();
		// A pure function of the seed and the slot, so it is computed once per slot and reused when the slot is.
		AvatarLoops.Add(MakeAvatarLoop(Settings.Seed, Slot));
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const double Now = World->GetTimeSeconds();
	ANCLPlayerAvatar* Avatar = World->SpawnActor<ANCLPlayerAvatar>(ANCLPlayerAvatar::StaticClass(),
		FTransform(LoopFacing(AvatarLoops[Slot], Now), LoopPosition(AvatarLoops[Slot], Now)), SpawnParams);
	check(Avatar);
	Avatar->SetPlayerSlot(static_cast<int8>(Slot));
	Avatars[Slot] = Avatar;
	AvatarOwners[Slot] = Player;
	UE_LOG(LogNetCostLab, Display, TEXT("NCL player joined: slot %d"), Slot);
	return Avatar;
}

void UNCLWorkloadSubsystem::RemovePlayer(AController* Exiting)
{
	const int32 Slot = AvatarOwners.IndexOfByPredicate([Exiting](const TWeakObjectPtr<AController>& Owner) { return Owner.Get() == Exiting; });
	if (Slot == INDEX_NONE)
	{
		return;
	}
	if (IsValid(Avatars[Slot]))
	{
		Avatars[Slot]->Destroy();
	}
	Avatars[Slot] = nullptr;
	AvatarOwners[Slot] = nullptr;
	UE_LOG(LogNetCostLab, Display, TEXT("NCL player left: slot %d"), Slot);
}

int8 UNCLWorkloadSubsystem::OccupiedSlotAt(float Fraction) const
{
	const int32 Occupied = static_cast<int32>(Algo::CountIf(Avatars, [](const TObjectPtr<ANCLPlayerAvatar>& Avatar) { return IsValid(Avatar); }));
	if (Occupied == 0)
	{
		return -1;
	}
	int32 Wanted = FMath::Min(static_cast<int32>(Fraction * Occupied), Occupied - 1);
	for (int32 Slot = 0; Slot < Avatars.Num(); ++Slot)
	{
		if (IsValid(Avatars[Slot]) && Wanted-- == 0)
		{
			return static_cast<int8>(Slot);
		}
	}
	return -1;
}

void UNCLWorkloadSubsystem::ApplyDroneChange(ANCLDrone& Drone, int32 Index, int32 Number) const
{
	using namespace NCLWorkload;

	// The values depend only on (seed, drone, change number). The target is drawn as a fraction of the players connected
	// now, so it is the same in every configuration once all N players have joined. The fraction is drawn even when
	// the drone does not attack, so the health draw is always the third value.
	const FRandomStream Stream = MakeStream(Settings.Seed, EStream::DroneState, Index, Number);
	const bool bAttack = Stream.FRand() < AttackProbability;
	const float TargetFraction = Stream.FRand();
	const int32 HealthLoss = Stream.RandRange(0, MaxHealthLoss);

	const int8 Target = bAttack ? OccupiedSlotAt(TargetFraction) : -1;      // -1 also when no player is connected
	const int32 NewHealth = Drone.GetState().Health - HealthLoss;
	Drone.ApplyStateChange(Target >= 0 ? 1 : 0, Target, static_cast<int16>(NewHealth > 0 ? NewHealth : FullHealth));
}

void UNCLWorkloadSubsystem::ApplyLoadoutChange(ANCLDrone& Drone, int32 Index, int32 Number) const
{
	const FRandomStream Stream = MakeStream(Settings.Seed, EStream::DroneLoadout, Index, Number);
	Drone.ApplyLoadoutChange(Stream.RandRange(0, 60), Stream.RandRange(0, 100),
		static_cast<uint8>(Stream.RandRange(0, 7)), static_cast<uint8>(Stream.RandRange(0, 1)));
}

void UNCLWorkloadSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	using namespace NCLWorkload;

	const UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();

	// The arrays are empty until the server spawns the workload, and always on a client, so these loops do nothing there.
	// Each element is read once into a raw pointer (a TObjectPtr read is not free in the editor binary).
	for (int32 Index = 0; Index < Drones.Num(); ++Index)
	{
		ANCLDrone* Drone = Drones[Index];
		if (!IsValid(Drone))
		{
			continue;
		}
		Drone->SetActorLocation(LoopPosition(DroneLoops[Index], Now));
		// At most one change of each kind per drone per tick. After a server hitch, every drone whose time passed
		// changes on the next tick (a burst that the server CSV shows), and the schedule keeps its phase.
		if (const int32 Number = TakeDue(StateSchedules[Index], Now, DroneChangePeriod); Number != INDEX_NONE)
		{
			ApplyDroneChange(*Drone, Index, Number);
		}
		if (const int32 Number = TakeDue(LoadoutSchedules[Index], Now, LoadoutChangePeriod); Number != INDEX_NONE)
		{
			ApplyLoadoutChange(*Drone, Index, Number);
		}
	}

	for (int32 Index = 0; Index < Props.Num(); ++Index)
	{
		// The schedule first: about one prop in 600 is due per tick, so most props are never touched.
		if (const int32 Number = TakeDue(ToggleSchedules[Index], Now, PropTogglePeriod); Number != INDEX_NONE)
		{
			if (ANCLProp* Prop = Props[Index]; IsValid(Prop))
			{
				Prop->ApplyToggle(OccupiedSlotAt(MakeStream(Settings.Seed, EStream::PropToggle, Index, Number).FRand()));
			}
		}
	}

	for (int32 Slot = 0; Slot < Avatars.Num(); ++Slot)
	{
		if (ANCLPlayerAvatar* Avatar = Avatars[Slot]; IsValid(Avatar))
		{
			Avatar->SetActorLocationAndRotation(LoopPosition(AvatarLoops[Slot], Now), LoopFacing(AvatarLoops[Slot], Now));
		}
	}

	if (Settings.CountLogInterval > 0.0 && Now >= NextCountLogTime)
	{
		NextCountLogTime = Now + Settings.CountLogInterval;
		LogCounts();
	}
}

void UNCLWorkloadSubsystem::LogCounts() const
{
	int32 NumDrones = 0;
	int32 NumProps = 0;
	int32 NumAvatars = 0;
	UWorld* World = GetWorld();
	for (TActorIterator<ANCLDrone> It(World); It; ++It) { ++NumDrones; }
	for (TActorIterator<ANCLProp> It(World); It; ++It) { ++NumProps; }
	for (TActorIterator<ANCLPlayerAvatar> It(World); It; ++It) { ++NumAvatars; }
	UE_LOG(LogNetCostLab, Display, TEXT("NCL counts: netmode=%d drones=%d props=%d avatars=%d"),
		static_cast<int32>(World->GetNetMode()), NumDrones, NumProps, NumAvatars);
}

TStatId UNCLWorkloadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNCLWorkloadSubsystem, STATGROUP_Tickables);
}
