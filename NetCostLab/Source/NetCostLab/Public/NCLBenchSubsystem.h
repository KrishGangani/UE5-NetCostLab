// P05 NetCostLab - the benchmark director: runs one timed, repeatable network run and measures it.

#pragma once

#include "Async/Future.h"
#include "Engine/EngineBaseTypes.h"
#include "NCLGameState.h"
#include "NCLHistogram.h"
#include "NCLWorkloadSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCLBenchSubsystem.generated.h"

class ANCLDrone;
class FJsonObject;
class UNetConnection;

/**
 * Server: waits until every expected client has joined, warms up, then opens the measured window itself
 * (FCsvProfiler::BeginCapture into -NCLOutDir), samples every connection's sent bytes and saturation after each net
 * flush (FWorldDelegates::OnWorldTickEnd), records every drone state change (the truth log), closes the window after
 * exactly -NCLCaptureFrames frames, freezes the workload's changes, lets the last ones settle, writes its report and
 * the final state of every actor, and quits once every client has left.
 * Client: follows the replicated phase (ANCLGameState). Over the window it measures its received bytes and frame rate,
 * and, every frame, each drone's position error and age against the true position (it rebuilds the seeded plan and maps
 * the shared clock to the server's world time); it records every drone state arrival. On Dump it writes its report,
 * its arrivals and the final state of every actor it holds, then quits.
 *
 * The measuring code lives here and only here; the workload never knows it is being measured, except that the director
 * freezes its gameplay changes for the settle period, and drones broadcast state arrivals that this class listens to.
 *
 * Command line (all optional; without -NCLClients the server's director stays idle):
 *   -NCLClients=N            clients to wait for (server)
 *   -NCLWarmup=SEC (20)      -NCLCaptureFrames=N (1800, i.e. 60 s at 30 Hz)      -NCLSettle=SEC (5)
 *   -NCLRunTag=NAME          file name prefix and CSV metadata      -NCLConfig=C1..C6 (a label: CSV metadata, report)
 *   -NCLOutDir=PATH          where the CSV and the reports go        -NCLClientIndex=K (client: its report's number)
 *   -NCLClientFPS=N          client: frame-rate cap (0 = none)
 *   -NCLExit                 quit when the run is finished
 */
UCLASS()
class UNCLBenchSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** One drone state change (server: the frame it was applied in) or arrival (client: when it was received). */
	struct FStateEvent
	{
		int16 DroneIndex = -1;
		uint16 Seq = 0;
		int8 TargetSlot = -1;
		double Clock = 0.0;
		/** Client: the first state of a drone actor that just appeared (it became relevant), not the delivery of a change. */
		bool bFirstState = false;
	};

	//~ Begin USubsystem / UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem / UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

protected:
	//~ Begin UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

private:
	struct FSettings
	{
		int32 ExpectedClients = 0;
		double WarmupSeconds = 20.0;
		int32 CaptureFrames = 1800;
		double SettleSeconds = 5.0;
		FString RunTag = TEXT("run");
		FString ConfigTag = TEXT("?");
		FString OutDir;
		int32 ClientIndex = -1;
		float ClientMaxFPS = 0.0f;
		bool bExitWhenDone = false;
	};

	/** One client connection, as seen by the server during the measured window. Saturated frames are the engine's own
	 *  per-connection count of frames that ended with the connection not ready to send (IsNetReady() == false). */
	struct FConnectionSample
	{
		TWeakObjectPtr<UNetConnection> Connection;
		int32 PlayerSlot = -1;
		int64 BytesAtStart = 0;
		int64 BytesLastFrame = 0;
		uint32 SaturatedAtStart = 0;
		uint32 SaturatedLastFrame = 0;
	};

	/** A client's correctness measurement for one class of drones (threat: attacking this client's avatar; ambient). */
	struct FDroneErrors
	{
		FNCLHistogram ErrorCm{10000};       // 1 cm bins up to 100 m
		FNCLHistogram AgeMs{5000};          // 1 ms bins up to 5 s
	};

	static FSettings ReadSettings();

	void TickServer(double Now);
	void TickClient();
	void EnterPhase(ENCLBenchPhase NewPhase);
	int32 CountJoinedPlayers() const;
	void BeginMeasuredWindow();
	void TakeBaseline();
	void SampleFrame();
	void EndMeasuredWindow();
	void OnWorldTickEnd(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);
	void WriteServerReport() const;

	void UpdateOwnSlot();
	void PrepareClientTruth(const FNCLRunInfo& Received);
	void SampleDroneErrors();
	void WriteClientReport() const;

	/** Both roles: a drone's state changed (server: applied; client: arrived). Feeds the truth log or the arrivals. */
	void OnDroneStateChanged(const ANCLDrone& Drone, bool bFirstState);
	/** Both roles, after the settle: one row per drone, prop and avatar in this world (kind, index, CRC, position). */
	void WriteState(const FString& Suffix) const;

	void SaveJson(const FString& Suffix, const TSharedRef<FJsonObject>& Report) const;
	void SaveText(const FString& Suffix, const FString& Text) const;
	ANCLGameState* GetBenchGameState() const;

	FSettings Settings;
	bool bServer = false;
	bool bDirecting = false;
	/** Server: the current phase. Client: the phase last seen on the replicated game state. */
	ENCLBenchPhase Phase = ENCLBenchPhase::Waiting;
	double PhaseEndTime = 0.0;
	FDelegateHandle TickEndHandle;
	FDelegateHandle StateChangedHandle;
	/** Server: the run info sent to the clients (seed, counts, hash, clock anchor). Client: the one received, once its
	 *  plan checked out. */
	FNCLRunInfo RunInfo;

	// Server, measured window. The window starts at the first net flush after BeginCapture took effect and ends after
	// exactly CaptureFrames flushes, so it covers the same frames as the CSV capture.
	TArray<FConnectionSample> Samples;
	/** One net tick (1 / NetServerMaxTickRate), read when the window opens: a frame longer than two is a hitch. */
	double NetTickSeconds = 1.0 / 30.0;
	bool bBaselineTaken = false;
	double WindowStartSeconds = 0.0;
	double WindowSeconds = 0.0;
	int32 WindowFrames = 0;
	int32 LongFrames = 0;
	TSharedFuture<FString> CsvFile;
	/** Server: every drone state change applied in the window (the truth log). */
	TArray<FStateEvent> Truth;
	/** Server: the largest drift of world time from the clock anchor in the window. World time clamps a frame longer
	 *  than MaxUndilatedFrameTime (WorldSettings.cpp:334-343), so after such a hitch the clients' mapping would be off;
	 *  a run with a drift above a few ms is invalid for correctness. */
	double MaxAnchorDriftMs = 0.0;

	// Client.
	int64 ClientBytesAtStart = 0;
	int64 ClientBytesAtEnd = 0;
	double ClientWindowStart = 0.0;
	double ClientWindowSeconds = 0.0;
	int32 ClientWindowFrames = 0;
	/** Client: the plan rebuilt from the received run info (empty until then, or if its hash did not match). */
	TArray<UNCLWorkloadSubsystem::FLoop> DroneLoops;
	bool bTruthFailed = false;
	int32 OwnSlot = -1;
	FDroneErrors Threat;
	FDroneErrors Ambient;
	TArray<FStateEvent> Arrivals;
};
