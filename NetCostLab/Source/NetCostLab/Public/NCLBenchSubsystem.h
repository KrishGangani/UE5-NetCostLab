// P05 NetCostLab - the benchmark director: runs one timed, repeatable network run and measures it.

#pragma once

#include "Async/Future.h"
#include "Engine/EngineBaseTypes.h"
#include "NCLGameState.h"
#include "Subsystems/WorldSubsystem.h"
#include "NCLBenchSubsystem.generated.h"

class FJsonObject;
class UNetConnection;

/**
 * Server: waits until every expected client has joined, warms up, then opens the measured window itself
 * (FCsvProfiler::BeginCapture into -NCLOutDir), samples every connection's sent bytes and saturation after each net
 * flush (FWorldDelegates::OnWorldTickEnd), closes the window after exactly -NCLCaptureFrames frames, freezes the
 * workload's changes, lets the last ones settle, writes its report and quits once every client has left.
 * Client: follows the replicated phase (ANCLGameState), measures its received bytes and frame rate over the window it
 * sees, writes its report on Dump and quits.
 *
 * The measuring code lives here and only here; the workload never knows it is being measured, except that the director
 * freezes its gameplay changes for the settle period.
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
	void WriteClientReport() const;
	void SaveJson(const FString& Suffix, const TSharedRef<FJsonObject>& Report) const;
	ANCLGameState* GetBenchGameState() const;

	FSettings Settings;
	bool bServer = false;
	bool bDirecting = false;
	/** Server: the current phase. Client: the phase last seen on the replicated game state. */
	ENCLBenchPhase Phase = ENCLBenchPhase::Waiting;
	double PhaseEndTime = 0.0;
	FDelegateHandle TickEndHandle;

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

	// Client.
	int64 ClientBytesAtStart = 0;
	int64 ClientBytesAtEnd = 0;
	double ClientWindowStart = 0.0;
	double ClientWindowSeconds = 0.0;
	int32 ClientWindowFrames = 0;
};
