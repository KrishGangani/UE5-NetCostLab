// P05 NetCostLab - the game state: carries the benchmark run phase to every client.

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

	ENCLBenchPhase GetBenchPhase() const { return BenchPhase; }

private:
	UPROPERTY(Replicated)
	ENCLBenchPhase BenchPhase = ENCLBenchPhase::Waiting;
};
