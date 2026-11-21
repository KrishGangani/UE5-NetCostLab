// P05 NetCostLab - the game state: carries the benchmark run phase, and what a client needs to know the truth, to every
// client.

#pragma once

#include "GameFramework/GameStateBase.h"
#include "NCLGameState.generated.h"

/** The phases of one benchmark run, decided by the server's director (UNCLBenchSubsystem). */
UENUM()
enum class ENCLBenchPhase : uint8
{
	/** Waiting for every expected client to join. */
	Waiting,
	/** Everyone is connected; nothing is measured yet. */
	WarmUp,
	/** The measured window: the server's CSV capture runs, clients record. */
	Capture,
	/** No more gameplay changes; the last ones reach every client. */
	Settle,
	/** Everyone writes their files; clients then quit. */
	Dump
};

/**
 * What a client needs to compute the true position of every drone at any instant (S3): the workload's seed and counts
 * (the client rebuilds the same plan and checks its hash), and one anchor between the shared clock and the server's
 * world time. Every process on this machine reads the same clock (FPlatformTime::Seconds, QueryPerformanceCounter), and
 * the server's world time advances by the same frame deltas as that clock, so
 *   server world time at instant t = WorldSecondsAtAnchor + (t - ClockAtAnchor).
 * Set once, when the measured window opens.
 */
USTRUCT()
struct FNCLRunInfo
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Seed = 0;

	UPROPERTY()
	int32 NumDrones = 0;

	UPROPERTY()
	int32 NumProps = 0;

	UPROPERTY()
	uint32 WorkloadHash = 0;

	/** FPlatformTime::Seconds() at the start of the server frame that opened the window (FApp::GetCurrentTime()). */
	UPROPERTY()
	double ClockAtAnchor = 0.0;

	/** The server's world time in that frame. */
	UPROPERTY()
	double WorldSecondsAtAnchor = 0.0;

	bool IsSet() const { return NumDrones > 0; }
};

/**
 * Headless clients have no other way to know when the measured window starts and ends, so the phase replicates.
 * The game state is always relevant in every system: bAlwaysRelevant (GameStateBase.cpp:26), and Iris does not filter
 * AInfo actors spatially (FilterConfigs Info=None, BaseEngine.ini:1519).
 */
UCLASS()
class ANCLGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: moves the run to a new phase and marks it dirty. */
	void SetBenchPhase(ENCLBenchPhase NewPhase);

	/** Server: publishes the run's workload and clock anchor (once, before the phase becomes Capture). */
	void SetRunInfo(const FNCLRunInfo& NewRunInfo);

	ENCLBenchPhase GetBenchPhase() const { return BenchPhase; }
	const FNCLRunInfo& GetRunInfo() const { return RunInfo; }

private:
	UPROPERTY(Replicated)
	ENCLBenchPhase BenchPhase = ENCLBenchPhase::Waiting;

	UPROPERTY(Replicated)
	FNCLRunInfo RunInfo;
};
