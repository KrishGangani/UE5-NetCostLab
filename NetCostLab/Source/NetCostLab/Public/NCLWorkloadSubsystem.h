// P05 NetCostLab - the workload: spawns and drives everything that replicates, identically in every configuration.

#pragma once

#include "Math/RandomStream.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCLWorkloadSubsystem.generated.h"

class AController;
class ANCLDrone;
class ANCLPlayerAvatar;
class ANCLProp;
class APlayerController;

/**
 * Server side: spawns the drones and props from a fixed seed, moves drones and player avatars as a pure function of
 * server time, and applies a seeded change schedule (calling MARK_PROPERTY_DIRTY_FROM_NAME through the actors).
 * Every random value is a pure function of the seed, the kind of event, the object and the event's number (MakeStream),
 * and nothing here depends on the replication system, so C1-C6 replicate exactly the same changes.
 * Client side: does nothing except an optional periodic count of what has arrived (-NCLCountLog=SEC), used by checks.
 *
 * A world subsystem, not a placed actor: the map is an engine template, so nothing would place a manager actor, and
 * a subsystem has exactly the world's lifetime (02 section 4.2).
 *
 * Command line (all optional): -NCLSeed=S (default 1), -NCLDrones=N (256), -NCLProps=N (768), -NCLCountLog=SEC (off).
 */
UCLASS()
class UNCLWorkloadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem / UWorldSubsystem
	/** Reads the command line. Here, not at begin play: in a standalone or listen-server world the host's player logs
	 *  in (AddPlayer) before the world begins play (UnrealEngine.cpp:16646 vs 16660). */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem / UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	/** Server: spawns an avatar for a new player in the lowest free slot. Returns null on a client or if slots ran out. */
	ANCLPlayerAvatar* AddPlayer(APlayerController* Player);

	/** Server: destroys the leaving player's avatar and frees its slot. */
	void RemovePlayer(AController* Exiting);

	/** CRC of the seed, the counts and every spawn parameter: equal hashes mean equal workloads (SC1). */
	uint32 GetWorkloadHash() const { return WorkloadHash; }

	/** A circle in the horizontal plane, travelled at a constant angular speed (negative = clockwise). */
	struct FLoop
	{
		FVector Center = FVector::ZeroVector;
		double Radius = 0.0;
		double AngularSpeed = 0.0;
		double Phase = 0.0;

		bool operator==(const FLoop&) const = default;
	};

	struct FSettings
	{
		int32 Seed = 1;
		int32 NumDrones = 256;
		int32 NumProps = 768;
		double CountLogInterval = 0.0;
	};

	/** Everything the seed decides, computed without a world. Pure, so the automation tests can check it (05 C2). */
	struct FPlan
	{
		TArray<FLoop> DroneLoops;
		TArray<FVector> PropPositions;
		uint32 Hash = 0;
	};

	/** What a random stream is for. Part of the stream's seed, so streams of different kinds never coincide. */
	enum class EStream : uint8
	{
		DroneState,
		DroneLoadout,
		PropToggle,
		AvatarLoop
	};

	/** The random stream of one event: a pure function of the seed, the kind, the object's index and the event's number
	 *  (0, 1, 2, ...). A change's values therefore never depend on tick timing, on the order in which other changes
	 *  happen in a tick, or on the configuration. */
	static FRandomStream MakeStream(int32 Seed, EStream Kind, int32 Index, int32 Number);

	/** One recurring change of one object: when it is next due, and how many times it has happened. */
	struct FSchedule
	{
		double NextTime = 0.0;
		int32 Count = 0;
	};

	/** If the change is due, schedules the next one a period after its due time (the phase is kept) and returns this
	 *  one's number; else INDEX_NONE. At most one per call, so one object never changes twice in a tick. */
	static int32 TakeDue(FSchedule& Schedule, double Now, double Period);

	static FPlan MakePlan(const FSettings& InSettings);
	static FLoop MakeAvatarLoop(int32 Seed, int32 Slot);
	static FVector LoopPosition(const FLoop& Loop, double Time);
	/** The direction of travel at Time (yaw only): an avatar looks where it walks. */
	static FRotator LoopFacing(const FLoop& Loop, double Time);

	/** Half the side of the square arena, in cm: a 200 m square centred on the origin (01 section 6.1). */
	static constexpr double ArenaHalfExtent = 10000.0;

protected:
	//~ Begin UWorldSubsystem
	/** Game and PIE worlds only, never the editor's own world. */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	static FSettings ReadSettings();
	static uint32 HashLoop(const FLoop& Loop, uint32 Hash);

	void SpawnWorkload(UWorld& World);
	void ApplyDroneChange(ANCLDrone& Drone, int32 Index, int32 Number) const;
	void ApplyLoadoutChange(ANCLDrone& Drone, int32 Index, int32 Number) const;
	/** A uniformly chosen occupied player slot (Fraction in [0,1)), or -1 if no player is connected. */
	int8 OccupiedSlotAt(float Fraction) const;
	void LogCounts() const;

	FSettings Settings;
	uint32 WorkloadHash = 0;
	double NextCountLogTime = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANCLDrone>> Drones;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANCLProp>> Props;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ANCLPlayerAvatar>> Avatars;

	/** The controller that owns each avatar slot. Weak: controllers belong to the game mode, not to us. */
	TArray<TWeakObjectPtr<AController>> AvatarOwners;

	/** Per drone, by index: its loop and the schedules of its state and loadout changes. */
	TArray<FLoop> DroneLoops;
	TArray<FSchedule> StateSchedules;
	TArray<FSchedule> LoadoutSchedules;

	/** Per prop, by index. */
	TArray<FSchedule> ToggleSchedules;

	/** Per player slot. */
	TArray<FLoop> AvatarLoops;
};
