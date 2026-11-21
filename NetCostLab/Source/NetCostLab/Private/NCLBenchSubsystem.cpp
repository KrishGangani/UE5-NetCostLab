// P05 NetCostLab - UNCLBenchSubsystem.

#include "NCLBenchSubsystem.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NCLLog.h"
#include "NCLPlayerAvatar.h"
#include "NCLWorkloadSubsystem.h"
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
	if (!bServer && Settings.ClientMaxFPS > 0.0f)
	{
		// Not -dpcvars=t.MaxFPS: UGameEngine::Init re-applies the user settings and sets t.MaxFPS to FrameRateLimit (0)
		// on every non-dedicated process (GameEngine.cpp:1249-1250, GameUserSettings.cpp:535-538). A client's world
		// begins play after that, so the cap set here stays.
		GEngine->SetMaxFPS(Settings.ClientMaxFPS);
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

#if CSV_PROFILER
	CSV_METADATA(TEXT("NCLRunTag"), *Settings.RunTag);
	CSV_METADATA(TEXT("NCLConfig"), *Settings.ConfigTag);
	CSV_METADATA(TEXT("NCLClients"), *LexToString(Samples.Num()));
	CSV_METADATA(TEXT("NCLWorkloadHash"), *NCLBench::WorkloadHashText(GetWorld()));
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
	UE_LOG(LogNetCostLab, Display, TEXT("NCL bench: window closed after %d frames, %.3f s, %d long frames"),
		WindowFrames, WindowSeconds, LongFrames);
}

void UNCLBenchSubsystem::SaveJson(const FString& Suffix, const TSharedRef<FJsonObject>& Report) const
{
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	const FString Path = FPaths::Combine(Settings.OutDir, Settings.RunTag + Suffix);
	if (!FFileHelper::SaveStringToFile(Json, *Path))
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
	Report->SetArrayField(TEXT("connections"), Connections);
	SaveJson(TEXT("_server.json"), Report);
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

	const ENCLBenchPhase Seen = GameState->GetBenchPhase();
	if (Seen != Phase)
	{
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
	}
	else if (Phase == ENCLBenchPhase::Dump)
	{
		WriteClientReport();
		bDirecting = false;      // the run is over for this client: no more work, and the report is written once
		if (Settings.bExitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("UNCLBenchSubsystem"));
		}
	}
}

void UNCLBenchSubsystem::WriteClientReport() const
{
	const ANCLPlayerAvatar* Avatar = NCLBench::ViewedAvatar(GetWorld()->GetFirstPlayerController());
	const int64 Bytes = ClientBytesAtEnd - ClientBytesAtStart;
	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("run_tag"), Settings.RunTag);
	Report->SetNumberField(TEXT("client_index"), Settings.ClientIndex);
	Report->SetNumberField(TEXT("slot"), Avatar ? Avatar->GetPlayerSlot() : -1);
	Report->SetNumberField(TEXT("in_bytes"), static_cast<double>(Bytes));
	Report->SetNumberField(TEXT("bytes_per_second"), ClientWindowSeconds > 0.0 ? Bytes / ClientWindowSeconds : 0.0);
	Report->SetNumberField(TEXT("window_seconds"), ClientWindowSeconds);
	Report->SetNumberField(TEXT("window_frames"), ClientWindowFrames);
	Report->SetNumberField(TEXT("fps"), ClientWindowSeconds > 0.0 ? ClientWindowFrames / ClientWindowSeconds : 0.0);
	SaveJson(FString::Printf(TEXT("_client%02d.json"), Settings.ClientIndex), Report);
}

TStatId UNCLBenchSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UNCLBenchSubsystem, STATGROUP_Tickables);
}
