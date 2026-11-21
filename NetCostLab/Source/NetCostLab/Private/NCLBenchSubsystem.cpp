// P05 NetCostLab - UNCLBenchSubsystem.

#include "NCLBenchSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NCLDrone.h"
#include "NCLFocusSubsystem.h"
#include "NCLLog.h"
#include "NCLPlayerAvatar.h"
#include "NCLPlayerController.h"
#include "NCLProp.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Stats/Stats.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NCLBenchSubsystem)

// Our per-frame CSV columns: NetCostLab/BusyMsPrevFrame, /OutBytes, /SatConnections, /Clients, and the BenchStart and
// BenchEnd events.
CSV_DEFINE_CATEGORY(NetCostLab, true);

namespace NCLBench
{
	/** After Dump the server waits until every client has written its report and disconnected, at most this long. */
	constexpr double ClientReportTimeoutSeconds = 30.0;
	/** A frame longer than this many net ticks is a hitch. Counted, so a run with hitches can be flagged. */
	constexpr double LongFrameTicks = 2.0;

	/** The avatar a player views (ANCLGameMode::PostLogin makes it the view target), or null before that. */
	const ANCLPlayerAvatar* ViewedAvatar(const APlayerController* Controller)
	{
		return Controller ? Cast<ANCLPlayerAvatar>(Controller->GetViewTarget()) : nullptr;
	}

	FString WorkloadHashText(const UWorld* World)
	{
		const UNCLWorkloadSubsystem* Workload = World->GetSubsystem<UNCLWorkloadSubsystem>();
		return FString::Printf(TEXT("0x%08X"), Workload ? Workload->GetWorkloadHash() : 0u);
	}

	/** Count, mean, percentiles and the non-empty bins of a histogram, for the client report and for merging later. */
	TSharedRef<FJsonObject> HistogramJson(const FNCLHistogram& Histogram)
	{
		const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("count"), static_cast<double>(Histogram.Count));
		Json->SetNumberField(TEXT("negative"), static_cast<double>(Histogram.NegativeCount));
		Json->SetNumberField(TEXT("mean"), Histogram.Mean());
		Json->SetNumberField(TEXT("p50"), Histogram.Percentile(50.0));
		Json->SetNumberField(TEXT("p95"), Histogram.Percentile(95.0));
		Json->SetNumberField(TEXT("p99"), Histogram.Percentile(99.0));
		Json->SetNumberField(TEXT("max"), Histogram.Max);
		TArray<TSharedPtr<FJsonValue>> Bins;
		for (int32 Bin = 0; Bin < Histogram.Bins.Num(); ++Bin)
		{
			if (Histogram.Bins[Bin] > 0)
			{
				Bins.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
					MakeShared<FJsonValueNumber>(Bin), MakeShared<FJsonValueNumber>(Histogram.Bins[Bin])}));
			}
		}
		Json->SetArrayField(TEXT("bins"), Bins);
		return Json;
	}

	/** One row per actor: kind (D drone, P prop, A avatar), index or slot, state CRC, position. */
	void AppendStateRow(FString& Text, TCHAR Kind, int32 Index, uint32 Crc, const FVector& Location)
	{
		Text.Appendf(TEXT("%c,%d,%u,%.1f,%.1f,%.1f\n"), Kind, Index, Crc, Location.X, Location.Y, Location.Z);
	}

	/** The truth log and the arrivals share one format: drone, Seq, target slot, shared-clock time, first state. */
	FString EventRows(const TArray<UNCLBenchSubsystem::FStateEvent>& Events)
	{
		FString Text = TEXT("drone,seq,target,clock,first\n");
		for (const UNCLBenchSubsystem::FStateEvent& Event : Events)
		{
			Text.Appendf(TEXT("%d,%u,%d,%.6f,%d\n"), Event.DroneIndex, Event.Seq, Event.TargetSlot, Event.Clock, Event.bFirstState ? 1 : 0);
		}
		return Text;
	}
}

bool UNCLBenchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game worlds only: in PIE several client worlds share one process and one command line, so their reports would
	// overwrite each other. Measured runs never use PIE.
	return WorldType == EWorldType::Game;
}

UNCLBenchSubsystem::FSettings UNCLBenchSubsystem::ReadSettings()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FSettings Read;
	FParse::Value(CommandLine, TEXT("NCLClients="), Read.ExpectedClients);
	FParse::Value(CommandLine, TEXT("NCLWarmup="), Read.WarmupSeconds);
	FParse::Value(CommandLine, TEXT("NCLCaptureFrames="), Read.CaptureFrames);
	FParse::Value(CommandLine, TEXT("NCLSettle="), Read.SettleSeconds);
	FParse::Value(CommandLine, TEXT("NCLRunTag="), Read.RunTag);
	FParse::Value(CommandLine, TEXT("NCLConfig="), Read.ConfigTag);
	FParse::Value(CommandLine, TEXT("NCLOutDir="), Read.OutDir);
	FParse::Value(CommandLine, TEXT("NCLClientIndex="), Read.ClientIndex);
	FParse::Value(CommandLine, TEXT("NCLClientFPS="), Read.ClientMaxFPS);
	Read.bExitWhenDone = FParse::Param(CommandLine, TEXT("NCLExit"));
	Read.CaptureFrames = FMath::Max(Read.CaptureFrames, 1);
	if (Read.OutDir.IsEmpty())
	{
		Read.OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NCLRuns"));
	}
	return Read;
}

void UNCLBenchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Settings = ReadSettings();
}

void UNCLBenchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bServer = InWorld.GetNetMode() != NM_Client;
	// The server directs only when told how many clients to expect; a client always follows (it costs nothing idle).
	bDirecting = !bServer || Settings.ExpectedClients > 0;
	if (!bServer)
	{
		if (Settings.ClientMaxFPS > 0.0f)
		{
			// Not -dpcvars=t.MaxFPS: UGameEngine::Init re-applies the user settings and sets t.MaxFPS to FrameRateLimit (0)
			// on every non-dedicated process (GameEngine.cpp:1249-1250, GameUserSettings.cpp:535-538). A client's world
			// begins play after that, so the cap set here stays.
			GEngine->SetMaxFPS(Settings.ClientMaxFPS);
		}
	}
	if (bDirecting)
	{
		StateChangedHandle = ANCLDrone::OnStateChanged.AddUObject(this, &UNCLBenchSubsystem::OnDroneStateChanged);
	}
	if (bDirecting && bServer)
	{
		TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &UNCLBenchSubsystem::OnWorldTickEnd);
		UE_LOG(LogNetCostLab, Display, TEXT("NCL bench: config=%s tag=%s waiting for %d clients, capture %d frames"),
			*Settings.ConfigTag, *Settings.RunTag, Settings.ExpectedClients, Settings.CaptureFrames);
	}
}

void UNCLBenchSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldTickEnd.Remove(TickEndHandle);
	ANCLDrone::OnStateChanged.Remove(StateChangedHandle);
	Super::Deinitialize();
}

ANCLGameState* UNCLBenchSubsystem::GetBenchGameState() const
{
	return GetWorld()->GetGameState<ANCLGameState>();
}

void UNCLBenchSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bDirecting)
	{
		return;
	}
	if (bServer)
	{
		TickServer(GetWorld()->GetTimeSeconds());
	}
	else
	{
		TickClient();
	}
}

int32 UNCLBenchSubsystem::CountJoinedPlayers() const
{
	const UNetDriver* NetDriver = GetWorld()->GetNetDriver();
	if (!NetDriver)
	{
		return 0;
	}
	int32 Joined = 0;
	for (const TObjectPtr<UNetConnection>& Connection : NetDriver->ClientConnections)
	{
		// Joined = its controller views its avatar (ANCLGameMode::PostLogin has run).
		Joined += (Connection && NCLBench::ViewedAvatar(Connection->PlayerController)) ? 1 : 0;
	}
	return Joined;
}

void UNCLBenchSubsystem::EnterPhase(ENCLBenchPhase NewPhase)
{
	Phase = NewPhase;
	if (ANCLGameState* GameState = GetBenchGameState())
	{
		GameState->SetBenchPhase(NewPhase);
	}
	UE_LOG(LogNetCostLab, Display, TEXT("NCL bench: phase %d at %.3f s"), static_cast<int32>(NewPhase), GetWorld()->GetTimeSeconds());
}

void UNCLBenchSubsystem::TickServer(double Now)
{
	switch (Phase)
	{
	case ENCLBenchPhase::Waiting:
		if (CountJoinedPlayers() >= Settings.ExpectedClients)
		{
			PhaseEndTime = Now + Settings.WarmupSeconds;
			EnterPhase(ENCLBenchPhase::WarmUp);
		}
		break;
	case ENCLBenchPhase::WarmUp:
		if (Now >= PhaseEndTime)
		{
			BeginMeasuredWindow();
			EnterPhase(ENCLBenchPhase::Capture);
		}
		break;
	case ENCLBenchPhase::Capture:
		break;      // frame by frame in OnWorldTickEnd, which also ends the window
	case ENCLBenchPhase::Settle:
		if (Now >= PhaseEndTime)
		{
			WriteServerReport();
			WriteState(TEXT("_server_state.csv"));
			PhaseEndTime = Now + NCLBench::ClientReportTimeoutSeconds;
			EnterPhase(ENCLBenchPhase::Dump);
		}
		break;
	case ENCLBenchPhase::Dump:
	{
		// Quit once every client has seen Dump, written its report and left (or after the timeout, e.g. under heavy
		// loss), and only when the CSV file is written (EndCapture writes it asynchronously).
		const UNetDriver* NetDriver = GetWorld()->GetNetDriver();
		const bool bClientsGone = !NetDriver || NetDriver->ClientConnections.IsEmpty();
		if (Settings.bExitWhenDone && (bClientsGone || Now >= PhaseEndTime) && (!CsvFile.IsValid() || CsvFile.IsReady()))
		{
			bDirecting = false;
			UE_LOG(LogNetCostLab, Display, TEXT("NCL bench: done (%s)"), bClientsGone ? TEXT("all clients left") : TEXT("timeout"));
			FPlatformMisc::RequestExit(false, TEXT("UNCLBenchSubsystem"));
		}
		break;
	}
	}
}

void UNCLBenchSubsystem::BeginMeasuredWindow()
{
	Samples.Reset();
	if (const UNetDriver* NetDriver = GetWorld()->GetNetDriver())
	{
		NetTickSeconds = 1.0 / FMath::Max(1, NetDriver->GetNetServerMaxTickRate());
		for (const TObjectPtr<UNetConnection>& Connection : NetDriver->ClientConnections)
		{
			if (!Connection)
			{
				continue;
			}
			FConnectionSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Connection = Connection;
			const ANCLPlayerAvatar* Avatar = NCLBench::ViewedAvatar(Connection->PlayerController);
			Sample.PlayerSlot = Avatar ? Avatar->GetPlayerSlot() : -1;
		}
	}
	bBaselineTaken = false;
	WindowFrames = 0;
	LongFrames = 0;

	// What a client needs to compute the truth itself: the workload, and the clock anchor of this frame. Room for every
	// change of the window: at most one per drone per second (twice the 2 s schedule), so no reallocation in it.
	Truth.Reset();
	MaxAnchorDriftMs = 0.0;
	if (const UNCLWorkloadSubsystem* Workload = GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>())
	{
		const UNCLWorkloadSubsystem::FSettings& Plan = Workload->GetSettings();
		Truth.Reserve(Plan.NumDrones * (FMath::CeilToInt(Settings.CaptureFrames * NetTickSeconds) + 2));
		RunInfo.Seed = Plan.Seed;
		RunInfo.NumDrones = Plan.NumDrones;
		RunInfo.NumProps = Plan.NumProps;
		RunInfo.WorkloadHash = Workload->GetWorkloadHash();
		RunInfo.ClockAtAnchor = FApp::GetCurrentTime();          // this frame's start, on the shared clock
		RunInfo.WorldSecondsAtAnchor = GetWorld()->GetTimeSeconds();
		if (ANCLGameState* GameState = GetBenchGameState())
		{
			GameState->SetRunInfo(RunInfo);
		}
	}

#if CSV_PROFILER
	CSV_METADATA(TEXT("NCLRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("NCLConfig"), *Settings.ConfigTag);
	CSV_METADATA(TEXT("NCLClients"), *LexToString(Samples.Num()));
	CSV_METADATA(TEXT("NCLWorkloadHash"), *NCLBench::WorkloadHashText(GetWorld()));
	ANCLDrone::ConsumeFocusBoostCount();      // the window counts only its own frames
	if (UNCLFocusSubsystem* Focus = GetWorld()->GetSubsystem<UNCLFocusSubsystem>())
	{
		Focus->ConsumeBoostCount();
	}
	// Takes effect at the start of the next frame (CsvProfiler.cpp:3870-3895); TakeBaseline runs at this frame's end.
	FCsvProfiler::Get()->BeginCapture(-1, Settings.OutDir, Settings.RunTag + TEXT("_server.csv"));
#endif
}

void UNCLBenchSubsystem::OnWorldTickEnd(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds)
{
	if (InWorld != GetWorld() || Phase != ENCLBenchPhase::Capture)
	{
		return;
	}
	// This runs after the frame's net flush (LevelTick.cpp:1929 then :2061), so the bytes sent this frame are counted.
	if (!bBaselineTaken)
	{
		TakeBaseline();       // the frame in which the capture was requested: not captured, not counted
		return;
	}
	SampleFrame();
	if (WindowFrames >= Settings.CaptureFrames)
	{
		EndMeasuredWindow();
	}
}

void UNCLBenchSubsystem::TakeBaseline()
{
	for (FConnectionSample& Sample : Samples)
	{
		if (const UNetConnection* Connection = Sample.Connection.Get())
		{
			Sample.BytesAtStart = Sample.BytesLastFrame = Connection->OutTotalBytes;
			Sample.SaturatedAtStart = Sample.SaturatedLastFrame = Connection->GetSaturationAnalytics().GetNumberOfSaturatedFrames();
		}
	}
	WindowStartSeconds = FPlatformTime::Seconds();
	bBaselineTaken = true;
}

void UNCLBenchSubsystem::SampleFrame()
{
	++WindowFrames;
	if (WindowFrames == 1)
	{
		CSV_EVENT(NetCostLab, TEXT("BenchStart"));      // the first captured frame: the capture is running now
	}

	const double FrameSeconds = FApp::GetDeltaTime();
	LongFrames += FrameSeconds > NCLBench::LongFrameTicks * NetTickSeconds ? 1 : 0;
	const double DriftMs = ((GetWorld()->GetTimeSeconds() - RunInfo.WorldSecondsAtAnchor) - (FApp::GetCurrentTime() - RunInfo.ClockAtAnchor)) * 1000.0;
	MaxAnchorDriftMs = FMath::Max(MaxAnchorDriftMs, FMath::Abs(DriftMs));
	int64 BytesThisFrame = 0;
	int32 SaturatedThisFrame = 0;
	for (FConnectionSample& Sample : Samples)
	{
		const UNetConnection* Connection = Sample.Connection.Get();
		if (!Connection)
		{
			continue;
		}
		BytesThisFrame += Connection->OutTotalBytes - Sample.BytesLastFrame;
		Sample.BytesLastFrame = Connection->OutTotalBytes;
		// The engine counts a frame as saturated when the connection ends its tick not ready to send
		// (SaturationAnalytics.TrackFrame(!IsNetReady()), NetConnection.cpp:5110): one step per tick at most.
		const uint32 Saturated = Connection->GetSaturationAnalytics().GetNumberOfSaturatedFrames();
		SaturatedThisFrame += Saturated != Sample.SaturatedLastFrame ? 1 : 0;
		Sample.SaturatedLastFrame = Saturated;
	}

	// The previous frame's busy time: its full delta minus the time the server slept at its end (UnrealEngine.cpp:3156).
	const double BusyMs = FMath::Max(0.0, FrameSeconds - FApp::GetIdleTime()) * 1000.0;
	CSV_CUSTOM_STAT(NetCostLab, BusyMsPrevFrame, static_cast<float>(BusyMs), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NetCostLab, OutBytes, static_cast<float>(BytesThisFrame), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NetCostLab, SatConnections, SaturatedThisFrame, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NetCostLab, Clients, Samples.Num(), ECsvCustomStatOp::Set);
	// C6 and C5: how often the focus rule raised a priority this frame (each is 0 in every other configuration).
	CSV_CUSTOM_STAT(NetCostLab, LegacyFocusBoosts, ANCLDrone::ConsumeFocusBoostCount(), ECsvCustomStatOp::Set);
	if (UNCLFocusSubsystem* Focus = GetWorld()->GetSubsystem<UNCLFocusSubsystem>())
	{
		CSV_CUSTOM_STAT(NetCostLab, GameplayFocusBoosts, Focus->ConsumeBoostCount(), ECsvCustomStatOp::Set);
	}
}

void UNCLBenchSubsystem::EndMeasuredWindow()
{
	WindowSeconds = FPlatformTime::Seconds() - WindowStartSeconds;
#if CSV_PROFILER
	CSV_EVENT(NetCostLab, TEXT("BenchEnd"));
	// Stops at the end of this frame, which is still captured (CsvProfiler.cpp:4208-4218).
	CsvFile = FCsvProfiler::Get()->EndCapture();
#endif
	if (UNCLWorkloadSubsystem* Workload = GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>())
	{
		Workload->FreezeChanges();
	}
	PhaseEndTime = GetWorld()->GetTimeSeconds() + Settings.SettleSeconds;
	EnterPhase(ENCLBenchPhase::Settle);
	UE_LOG(LogNetCostLab, Display, TEXT("NCL bench: window closed after %d frames, %.3f s, %d long frames, %d state changes"),
		WindowFrames, WindowSeconds, LongFrames, Truth.Num());
}

void UNCLBenchSubsystem::SaveJson(const FString& Suffix, const TSharedRef<FJsonObject>& Report) const
{
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	SaveText(Suffix, Json);
}

void UNCLBenchSubsystem::SaveText(const FString& Suffix, const FString& Text) const
{
	const FString Path = FPaths::Combine(Settings.OutDir, Settings.RunTag + Suffix);
	if (!FFileHelper::SaveStringToFile(Text, *Path))
	{
		UE_LOG(LogNetCostLab, Error, TEXT("NCL bench: could not write %s"), *Path);
	}
}

void UNCLBenchSubsystem::WriteServerReport() const
{
	int32 SaturatedConnectionFrames = 0;
	TArray<TSharedPtr<FJsonValue>> Connections;
	for (const FConnectionSample& Sample : Samples)
	{
		const int64 Bytes = Sample.BytesLastFrame - Sample.BytesAtStart;
		const int32 SaturatedFrames = static_cast<int32>(Sample.SaturatedLastFrame - Sample.SaturatedAtStart);
		SaturatedConnectionFrames += SaturatedFrames;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("slot"), Sample.PlayerSlot);
		Entry->SetNumberField(TEXT("out_bytes"), static_cast<double>(Bytes));
		Entry->SetNumberField(TEXT("bytes_per_second"), WindowSeconds > 0.0 ? Bytes / WindowSeconds : 0.0);
		Entry->SetNumberField(TEXT("saturated_frames"), SaturatedFrames);
		Connections.Add(MakeShared<FJsonValueObject>(Entry));
	}

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("run_tag"), Settings.RunTag);
	Report->SetStringField(TEXT("config"), Settings.ConfigTag);
	Report->SetStringField(TEXT("workload_hash"), NCLBench::WorkloadHashText(GetWorld()));
	Report->SetNumberField(TEXT("window_seconds"), WindowSeconds);
	Report->SetNumberField(TEXT("window_frames"), WindowFrames);
	Report->SetNumberField(TEXT("long_frames"), LongFrames);
	Report->SetNumberField(TEXT("saturated_connection_frames"), SaturatedConnectionFrames);
	Report->SetNumberField(TEXT("state_changes"), Truth.Num());
	Report->SetNumberField(TEXT("anchor_drift_ms"), MaxAnchorDriftMs);
	if (const UNCLFocusSubsystem* Focus = GetWorld()->GetSubsystem<UNCLFocusSubsystem>(); Focus && Focus->IsActive())
	{
		Report->SetNumberField(TEXT("c5_assigned"), Focus->GetAssignedCount());
		Report->SetNumberField(TEXT("c5_pending"), Focus->GetPendingCount());
		Report->SetNumberField(TEXT("c5_assign_failures"), Focus->GetAssignFailures());
	}
	Report->SetArrayField(TEXT("connections"), Connections);
	SaveJson(TEXT("_server.json"), Report);

	SaveText(TEXT("_server_truth.csv"), NCLBench::EventRows(Truth));
}

void UNCLBenchSubsystem::OnDroneStateChanged(const ANCLDrone& Drone, bool bFirstState)
{
	if (Drone.GetWorld() != GetWorld())
	{
		return;     // one static event serves every world
	}
	const FNCLDroneState& State = Drone.GetState();
	if (bServer)
	{
		// The truth: every change applied while the window runs, timed at the start of the frame that applied it (the
		// clock the world time of this frame belongs to).
		if (Phase == ENCLBenchPhase::Capture)
		{
			Truth.Add({Drone.GetDroneIndex(), State.Seq, State.TargetSlot, FApp::GetCurrentTime(), false});
		}
	}
	else if (Phase != ENCLBenchPhase::Waiting && Phase != ENCLBenchPhase::Dump)
	{
		// From the warm-up on, a full warm-up before the window, since the client's phase lags the server's; only the
		// arrivals that match a change of the truth log count (divergence.py).
		Arrivals.Add({Drone.GetDroneIndex(), State.Seq, State.TargetSlot, FPlatformTime::Seconds(), bFirstState});
	}
}

void UNCLBenchSubsystem::WriteState(const FString& Suffix) const
{
	FString Text = TEXT("kind,index,crc,x,y,z\n");
	for (TActorIterator<ANCLDrone> It(GetWorld()); It; ++It)
	{
		NCLBench::AppendStateRow(Text, TEXT('D'), It->GetDroneIndex(), It->GetStateCrc(), It->GetActorLocation());
	}
	for (TActorIterator<ANCLProp> It(GetWorld()); It; ++It)
	{
		NCLBench::AppendStateRow(Text, TEXT('P'), It->GetPropIndex(), It->GetStateCrc(), It->GetActorLocation());
	}
	for (TActorIterator<ANCLPlayerAvatar> It(GetWorld()); It; ++It)
	{
		NCLBench::AppendStateRow(Text, TEXT('A'), It->GetPlayerSlot(), 0, It->GetActorLocation());
	}
	SaveText(Suffix, Text);
}

void UNCLBenchSubsystem::TickClient()
{
	const ANCLGameState* GameState = GetBenchGameState();
	const UNetDriver* NetDriver = GetWorld()->GetNetDriver();
	const UNetConnection* ServerConnection = NetDriver ? NetDriver->ServerConnection.Get() : nullptr;
	if (!GameState || !ServerConnection)
	{
		return;
	}

	if (OwnSlot < 0)
	{
		UpdateOwnSlot();        // once the controller's slot has arrived
	}
	const ENCLBenchPhase Seen = GameState->GetBenchPhase();
	if (Seen != Phase)
	{
		if (Phase == ENCLBenchPhase::Waiting)
		{
			Arrivals.Reserve(32768);     // arrivals are recorded from here on (OnDroneStateChanged)
		}
		if (Seen == ENCLBenchPhase::Capture)
		{
			ClientBytesAtStart = ServerConnection->InTotalBytes;
			ClientWindowStart = FPlatformTime::Seconds();
			ClientWindowFrames = 0;
		}
		else if (Phase == ENCLBenchPhase::Capture)
		{
			ClientBytesAtEnd = ServerConnection->InTotalBytes;
			ClientWindowSeconds = FPlatformTime::Seconds() - ClientWindowStart;
		}
		Phase = Seen;
	}

	if (Phase == ENCLBenchPhase::Capture)
	{
		++ClientWindowFrames;
		// The run info replicates with the phase; once it is here (and its plan checks out), measure every frame.
		if (DroneLoops.IsEmpty() && !bTruthFailed && GameState->GetRunInfo().IsSet())
		{
			PrepareClientTruth(GameState->GetRunInfo());
		}
		SampleDroneErrors();
	}
	else if (Phase == ENCLBenchPhase::Dump)
	{
		WriteClientReport();
		WriteState(FString::Printf(TEXT("_client%02d_state.csv"), Settings.ClientIndex));
		bDirecting = false;      // the run is over for this client: no more work, and the files are written once
		if (Settings.bExitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("UNCLBenchSubsystem"));
		}
	}
}

void UNCLBenchSubsystem::UpdateOwnSlot()
{
	// From the controller's replicated slot, not the view target (unset on Iris clients, NCLPlayerController.h).
	const ANCLPlayerController* Controller = Cast<ANCLPlayerController>(GetWorld()->GetFirstPlayerController());
	OwnSlot = Controller ? Controller->GetPlayerSlot() : -1;
}

void UNCLBenchSubsystem::PrepareClientTruth(const FNCLRunInfo& Received)
{
	// The client rebuilds the server's plan from the same seed and counts; the hash proves it is the same workload.
	UNCLWorkloadSubsystem::FSettings Plan;
	Plan.Seed = Received.Seed;
	Plan.NumDrones = Received.NumDrones;
	Plan.NumProps = Received.NumProps;
	UNCLWorkloadSubsystem::FPlan Rebuilt = UNCLWorkloadSubsystem::MakePlan(Plan);
	if (Rebuilt.Hash != Received.WorkloadHash)
	{
		UE_LOG(LogNetCostLab, Error, TEXT("NCL bench: the client's plan hash 0x%08X differs from the server's 0x%08X; no errors measured"),
			Rebuilt.Hash, Received.WorkloadHash);
		bTruthFailed = true;       // logged once; the report says truth_ready = false
		return;
	}
	DroneLoops = MoveTemp(Rebuilt.DroneLoops);
	RunInfo = Received;
}

void UNCLBenchSubsystem::SampleDroneErrors()
{
	if (DroneLoops.IsEmpty())
	{
		return;
	}
	// The server's world time now, from the shared clock (FNCLRunInfo): the instant the true position belongs to.
	const double ServerTime = RunInfo.WorldSecondsAtAnchor + (FPlatformTime::Seconds() - RunInfo.ClockAtAnchor);
	for (TActorIterator<ANCLDrone> It(GetWorld()); It; ++It)
	{
		const ANCLDrone* Drone = *It;
		const int32 Index = Drone->GetDroneIndex();
		if (!DroneLoops.IsValidIndex(Index))
		{
			continue;       // its index has not arrived yet
		}
		const UNCLWorkloadSubsystem::FLoop& Loop = DroneLoops[Index];
		const FVector Shown = Drone->GetActorLocation();
		// Classified by the target this client has received (it cannot know the server's current one); divergence.py
		// classifies state changes by the server's target. A retarget counts as threat once it has arrived.
		FDroneErrors& Class = (OwnSlot >= 0 && Drone->GetState().TargetSlot == OwnSlot) ? Threat : Ambient;
		Class.ErrorCm.Add(FVector::Dist(Shown, UNCLWorkloadSubsystem::LoopPosition(Loop, ServerTime)));
		Class.AgeMs.Add(UNCLWorkloadSubsystem::LoopAgeSeconds(Loop, Shown, ServerTime) * 1000.0);
	}
}

void UNCLBenchSubsystem::WriteClientReport() const
{
	const int64 Bytes = ClientBytesAtEnd - ClientBytesAtStart;
	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("run_tag"), Settings.RunTag);
	Report->SetNumberField(TEXT("client_index"), Settings.ClientIndex);
	Report->SetNumberField(TEXT("slot"), OwnSlot);
	Report->SetNumberField(TEXT("in_bytes"), static_cast<double>(Bytes));
	Report->SetNumberField(TEXT("bytes_per_second"), ClientWindowSeconds > 0.0 ? Bytes / ClientWindowSeconds : 0.0);
	Report->SetNumberField(TEXT("window_seconds"), ClientWindowSeconds);
	Report->SetNumberField(TEXT("window_frames"), ClientWindowFrames);
	Report->SetNumberField(TEXT("fps"), ClientWindowSeconds > 0.0 ? ClientWindowFrames / ClientWindowSeconds : 0.0);
	Report->SetBoolField(TEXT("truth_ready"), !DroneLoops.IsEmpty());
	Report->SetNumberField(TEXT("state_arrivals"), Arrivals.Num());
	Report->SetObjectField(TEXT("threat_error_cm"), NCLBench::HistogramJson(Threat.ErrorCm));
	Report->SetObjectField(TEXT("threat_age_ms"), NCLBench::HistogramJson(Threat.AgeMs));
	Report->SetObjectField(TEXT("ambient_error_cm"), NCLBench::HistogramJson(Ambient.ErrorCm));
	Report->SetObjectField(TEXT("ambient_age_ms"), NCLBench::HistogramJson(Ambient.AgeMs));
	SaveJson(FString::Printf(TEXT("_client%02d.json"), Settings.ClientIndex), Report);

	SaveText(FString::Printf(TEXT("_client%02d_arrivals.csv"), Settings.ClientIndex), NCLBench::EventRows(Arrivals));
}

TStatId UNCLBenchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNCLBenchSubsystem, STATGROUP_Tickables);
}
