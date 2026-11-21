# 05 · Line By Line Code Explanation: NetCostLab

**Rule:** every file that exists in `NetCostLab\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F3 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, config `.ini` files, `Tools\*.bat` |
| B | Phase 2 probe and Phase 4 check F4 | `Tools\netlab_env.py`, `Tools\probe_net.py`, `Tools\foundation_job.py`, the removed probe `NetCostLabServer.Target.cs` |
| C | Phase 5, S1 (the workload) | `NetCostLab.Build.cs` (update), `NCLLog.h`, `NCLNet.h`, module `.cpp` (update), `NCLDrone`, `NCLProp`, `NCLPlayerAvatar`, `NCLPlayerController`, `NCLGameMode`, `NCLWorkloadSubsystem`, `Tests\NCLWorkloadTests.cpp`, `Config\DefaultEngine.ini` (game mode), `Tools\check_job.py`, `Tools\run_tests.bat`, `Tools\probe_net.py`, `Tools\netlab_env.py`, `Tools\foundation_job.py` (updates) |
| D | Phase 5, S2 (director, telemetry, run script) | `NCLGameState`, `NCLBenchSubsystem`, `NCLGameMode` and `NCLWorkloadSubsystem` (updates), `NetCostLab.Build.cs` (`Json`), `Tools\run_bench.py`, `Tools\csv_stats.py`, `Tools\check_job.py` (`--bench`) |

---

## Part A · Foundation (Phase 4)

### A1. `NetCostLab.uproject`: the project descriptor

```json
{
	"FileVersion": 3,
	"EngineAssociation": "5.8",
	"Category": "",
	"Description": "P05 NetCostLab: ...",
	"Modules": [ { "Name": "NetCostLab", "Type": "Runtime", "LoadingPhase": "Default" } ],
	"Plugins": [
		{ "Name": "NetCostLabCore",   "Enabled": true },
		{ "Name": "Iris",             "Enabled": true },
		{ "Name": "ReplicationGraph", "Enabled": true },
		{ "Name": "AndroidFileServer", "Enabled": false }
	]
}
```

| Line | What it does, and why |
|---|---|
| `"FileVersion": 3` | The descriptor schema version UE 5.8 expects; Epic's `TP_Blank.uproject:2` uses 3. A wrong value makes the editor refuse or upgrade the file |
| `"EngineAssociation": "5.8"` | Which installed engine opens the project. A launcher engine is identified by its version string; a blank value would make the shell ask every time |
| `"Description"` | Shown in the project browser. Cosmetic |
| `"Modules"` | The C++ modules the **project** owns. UnrealBuildTool compiles these plus the plugin modules |
| `"Name": "NetCostLab"` | Must match the folder `Source\NetCostLab\` and the class in `NetCostLab.Build.cs`. Module names are unique per project (`RulesAssembly.cs:199–206` keys them case-insensitively and skips duplicates) |
| `"Type": "Runtime"` | Built into game **and** editor targets. We need both: the packaged game is the client, the editor binary is the server (`01b` #3). `Editor` would leave the code out of packaged builds |
| `"LoadingPhase": "Default"` | Loads after engine start-up and before the first world exists, early enough for its UClasses (game mode, subsystems from S1) to register before the map loads |
| `"NetCostLabCore"` | Our plugin. It is `EnabledByDefault`, so it would load anyway; listing it keeps the dependency visible in one place |
| `"Iris"` | The engine plugin that loads `IrisCore` at start-up (`Plugins/Experimental/Iris/Source/Iris/Private/Iris/IrisModule.cpp:9–11`). Without it, `-UseIrisReplication=1` is refused with an ensure (`UnrealEngine.cpp:14878–14885`), so C4 and C5 could not run. `EnabledByDefault: false` in 5.8.4, so it must be listed |
| `"ReplicationGraph"` | The engine plugin that provides `UBasicReplicationGraph` for C3. Also `EnabledByDefault: false`. Enabling it does nothing unless a run sets `ReplicationDriverClassName` (C3 only) |
| `"AndroidFileServer": false` | An engine plugin that is `EnabledByDefault: true` (`Engine/Plugins/Runtime/AndroidFileServer/AndroidFileServer.uplugin:15`). Its editor module (`:24–28`, loaded `PostEngineInit` on Win64) writes an `[/Script/AndroidFileServerEditor.AndroidFileServerRuntimeSettings]` block with a **random `SecurityToken`** into `DefaultEngine.ini` the first time the editor runs on a project (UE5-UpdateBudget's `DefaultEngine.ini` has one). A token has no place in a public repository, and this project never deploys to Android, so the plugin is turned off before the editor ever runs on the project (`07` F13) |
| *Not listed:* a `"TargetPlatforms"` filter, Enhanced Input | No reason to restrict platforms; Enhanced Input is an engine default plugin (`DefaultInput.ini`, A7) |

### A2. `Source\NetCostLab.Target.cs` and `NetCostLabEditor.Target.cs`: build targets

```csharp
using UnrealBuildTool;

public class NetCostLabTarget : TargetRules
{
	public NetCostLabTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("NetCostLab");
	}
}
```

| Line | What it does, and why |
|---|---|
| `using UnrealBuildTool;` | Target files are **C#** run by UnrealBuildTool, not C++. This brings `TargetRules`, `TargetType` and the version enums into scope |
| `class NetCostLabTarget : TargetRules` | UnrealBuildTool finds targets by class name `<Name>Target`: this defines the target `NetCostLab`, which is why `build.bat NetCostLab` works |
| `(TargetInfo Target) : base(Target)` | UnrealBuildTool passes the platform, configuration and architecture; the base constructor stores them (`04` §5) |
| `Type = TargetType.Game` | A standalone game executable, the one that gets packaged. **In this project it is the headless client.** The editor file uses `TargetType.Editor` |
| `DefaultBuildSettings = BuildSettingsVersion.V7` | The newest default build settings; what Epic's 5.8 `TP_Blank.Target.cs` uses |
| `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8` | Pins 5.8's engine header include order, so an engine upgrade does not silently change what our code includes |
| `ExtraModuleNames.Add("NetCostLab")` | The project module to build; plugin modules come in through the plugin system |
| *Not set:* `bWithPushModel = true` | It would be refused: the property needs a unique build environment, which an installed engine does not allow (`TargetRules.cs:1521`, `RulesAssembly.cs:710–712`, `01b` #2). Push model is measured in the editor binary instead |

**The editor target** differs in two places: `TargetType.Editor`, and its header comment. Its DLLs are what `UnrealEditor-Cmd.exe` loads, both for editing and when it runs as our dedicated server with `-server`. Editor targets get `WITH_PUSH_MODEL=1` by default (`TargetRules.cs:1524`), which is exactly why the server is the editor binary.

**Why there is no `NetCostLabServer.Target.cs` any more:** the Phase 2 probe P2-1 added one and asked UnrealBuildTool to build it; the build was refused before compiling anything (`09`, P2-1). F1 deleted it, so `build.bat` cannot be pointed at a target that cannot exist. Its text is kept in Part B for the record.

### A3. `Source\NetCostLab\NetCostLab.Build.cs`: the game module's build rules

```csharp
public class NetCostLab : ModuleRules
{
	public NetCostLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
```

| Line | What it does, and why |
|---|---|
| `class NetCostLab : ModuleRules` | The class name **must equal the module name**; UnrealBuildTool maps each `*.Build.cs` to its module by file name |
| `ReadOnlyTargetRules Target` | A module may read target settings but not change them, so module rules cannot fight each other |
| `PCHUsage = UseExplicitOrSharedPCHs` | Use the engine's shared precompiled header unless the module names its own: faster compiles. *Rejected:* `NoPCHs` (slow) or a private PCH (pointless for a small module) |
| `Core`, `CoreUObject`, `Engine` (public) | Containers and logging; UObject and reflection; actors and worlds. Public because S1's public headers (the drone and pawn classes) will include engine types |
| *Not yet:* `NetCore`, `IrisCore` (`SetupIrisSupport`), `NetCostLabCore` | **No dependency without code that needs it** (`06` §2). S1 adds `NetCore` (push-model macros, `FNetHandle`) and Iris support for the C5 assignment code; S7 adds the plugin |

### A4. `Source\NetCostLab\Private\NetCostLabModule.cpp`

```cpp
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, NetCostLab, "NetCostLab");
```

| Line | What it does, and why |
|---|---|
| `#include "Modules/ModuleManager.h"` | Declares `IMPLEMENT_PRIMARY_GAME_MODULE` and `FDefaultGameModuleImpl`. Include the specific header (`04` §4) |
| `IMPLEMENT_PRIMARY_GAME_MODULE(…)` | Generates the module's entry point (`04` §1, `06` §1). In monolithic (packaged) builds it also defines `GInternalProjectName` and `GIsGameAgnosticExe = false` (`ModuleManager.h:1100–1136`). A project needs one primary game module; without it a packaged game has no project-name global to start from |
| `FDefaultGameModuleImpl` | The engine's module class with empty start-up and shut-down. We have no start-up logic, so a custom class would be a class with no reason |
| `NetCostLab` | The module name; must match the `.uproject` and `Build.cs` |
| `"NetCostLab"` | The old "game name" argument. Ignored in 5.8 (the macro parameter is named `DEPRECATED_GameName`); passed anyway because the macro still takes three arguments |

**Why there is no `NetCostLab.h`:** an empty module header that nothing includes adds nothing.

### A5. `Plugins\NetCostLabCore\NetCostLabCore.uplugin`: the plugin descriptor

| Field | Value | Why |
|---|---|---|
| `FileVersion`, `Version`, `VersionName` | 3, 1, "1.0" | The same schema as Epic's plugins in 5.8 (`ReplicationGraph.uplugin`) |
| `FriendlyName`, `Description`, `Category` | "NetCostLab Core", …, "Networking" | Shown in the Plugins window; the same category as Epic's Iris plugin |
| `EnabledByDefault` | `true` | A project that copies the plugin gets it without editing its `.uproject` |
| `CanContainContent` | `false` | Code only; avoids an empty content mount point |
| `IsBetaVersion`, `IsExperimentalVersion` | `false`, `false` | No warning badge. Honest about stability of our code, not a production-readiness claim |
| `Installed` | `false` | Part of the project, not installed into the engine |
| `Modules[0]` | `NetCostLabCore`, `Runtime`, `Default` | One runtime module: the prioritiser must exist on the server in every build type |
| `Plugins` | `[{ "Name": "Iris", "Enabled": true }]` | A plugin can declare the plugins it needs, in the same format the engine uses (`Engine/Plugins/Runtime/ADM/ADM.uplugin:27–32`). An Iris prioritiser is useless without Iris, so any project that copies this plugin gets Iris enabled with it |

### A6. `NetCostLabCore.Build.cs` and `Private\NetCostLabCoreModule.cpp`

```csharp
PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
SetupIrisSupport(Target);
```

| Line | What it does, and why |
|---|---|
| `Core`, `CoreUObject`, `Engine` (public) | The prioritiser's public header (S6) will derive from an Iris UClass and use engine types, so modules that include it need these paths |
| `SetupIrisSupport(Target)` | Adds a **private** `IrisCore` dependency and the `UE_WITH_IRIS=1` / `UE_NET_HAS_IRIS_FASTARRAY_BINDING=1` defines (`ModuleRules.cs:1747–1762`). Present from Phase 4, unlike the game module's network dependencies, because this plugin's only job is to extend Iris: the first build already proves the Iris dependency resolves against the installed engine. Private by default; S6 decides whether the public header forces it public |
| `IMPLEMENT_MODULE(FDefaultModuleImpl, NetCostLabCore);` | The non-primary module entry point (`06` §1); `FDefaultModuleImpl` is the empty default |

### A7. `Config\DefaultEngine.ini`, `DefaultGame.ini`, `DefaultInput.ini`

**`DefaultEngine.ini`**

| Section / key | Why |
|---|---|
| `GameMapsSettings: GameDefaultMap / ServerDefaultMap / EditorStartupMap = /Engine/Maps/Templates/Template_Default` | A shipped engine map with a floor and lights; no map is authored, so the project stays buildable from text. `ServerDefaultMap` is what a dedicated server opens without a map argument. The template's World Partition map was rejected: streaming would add noise |
| `[/Script/Engine.Engine] bSmoothFrameRate=False, bUseFixedFrameRate=False` | Restated engine defaults (`BaseEngine.ini:351`): smoothing or a fixed frame rate would decouple frame time from real work |
| `[SystemSettings] r.VSync=0` | Restated (`BaseEngine.ini:2481`). Clients are headless, but a rendering demo client (S8) must not wait for a display either |
| `[SystemSettings] net.SubObjects.DefaultUseSubObjectReplicationList=1` | **Added after probe P2-1** (`09` P2-1d). Iris requires actors to replicate their sub-objects through the *registered sub-object list* (the modern path, instead of the virtual `ReplicateSubobjects`); without this the Iris bridge raises a handled ensure that names exactly this line (`EngineReplicationBridge.cpp:234`). The cvar defaults to false (`Components/ActorComponent.cpp:115–117`) and every actor copies it into `bReplicateUsingRegisteredSubObjectList` in its **constructor** (`Actor.cpp:292`), so it must be set in config before any actor exists; a `-dpcvars` value per run would be too late for class default objects. It is set for **all five** configurations (legacy and Replication Graph support the registered list too), so the workload stays identical. *Rejected:* setting it only for Iris runs, which would make C4/C5 differ from C1–C3 in one more way |
| `[/Script/OnlineSubsystemUtils.IpNetDriver] NetServerMaxTickRate=30` | Restated (`BaseEngine.ini:1867`). The dedicated server's tick rate comes from here (`GameEngine.cpp:1719–1745`); writing it down makes the 30 Hz in every result visible in the project |
| `RendererSettings`, `WindowsTargetSettings` | Copied from Epic's 5.8 `TP_Blank` template (`Templates/TP_Blank/Config/DefaultEngine.ini:6–13`): DX12, Shader Model 6, virtual shadow maps, no static lighting. Standard defaults; nothing is rendered in a measured run |

Bandwidth caps, loss, push model and the replication system are **not** in this file: they differ per configuration and scenario, so the run scripts pass them on the command line, where the manifest records them.

**`DefaultGame.ini`:** the project name and description.

**`DefaultInput.ini`:** only the two keys Epic's 5.8 template sets for Enhanced Input (`Templates/TP_Blank/Config/DefaultInput.ini:79–80`): `DefaultPlayerInputClass=/Script/EnhancedInput.EnhancedPlayerInput` and `DefaultInputComponentClass=/Script/EnhancedInput.EnhancedInputComponent`. Without them the engine creates the classic `UPlayerInput` (`InputSettings.h:219, 223`), and the default-enabled Enhanced Input plugin (`EnhancedInput.uplugin:13`) logs "…does not have a valid PlayerInput object. Failed to load user settings." (`EnhancedInputSubsystems.cpp:115–119`) in every client. Other portfolio projects hit that warning in their first packaged run; we add the fix up front.

### A8. `Tools\build.bat`, `package.bat`, `smoke_game.bat`

All three: `@echo off`, a header with usage, `setlocal` (variables do not leak into the caller), `%~dp0` (the script's own folder, so they run from anywhere), quoted paths (the portfolio path contains a space), and `exit /b %ERRORLEVEL%` (the queue sees the real exit code). Stored with CRLF line endings (`.gitattributes`): `cmd.exe` can fail to find `goto` labels in LF-only files. Run **only through `tools\ue_queue.py`**.

| Script | Key lines | Why |
|---|---|---|
| `build.bat [Target] [Config]` | `call "%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" %TARGET% Win64 %CONFIG% -Project=… -WaitMutex -NoHotReloadFromIDE` | A reproducible command-line build. `-WaitMutex` waits for another build instead of failing. The header says why there is no Server target |
| `package.bat [Config]` | `RunUAT.bat BuildCookRun … -clientconfig=%CONFIG% -build -cook -stage -pak -archive -archivedirectory=…` | Build, cook, stage, pak and archive the packaged game. Development by default (the launcher engine cannot build Test). The header says the package is for the **clients** only |
| `smoke_game.bat` / `smoke_game.bat packaged` | Uncooked: `UnrealEditor-Cmd.exe <uproject> -game -nullrhi …`. Packaged: the **real** exe `Build\Windows\NetCostLab\Binaries\Win64\NetCostLab.exe -nullrhi -nosound -unattended -traceautostart=0 -ExecCmds="quit" -abslog=…` | Starts, loads the default map, quits. The packaged variant runs headless because that is how the benchmark clients run, and it starts the real exe, not the bootstrap, so the exit code is the game's own (`07` F10). The network checks are `probe_net.py` (Part B) |

---

## Part B · The Phase 2 probe and the Phase 4 network check

### B1. `Tools\netlab_env.py`: shared helpers

| Piece | What it does, and why |
|---|---|
| `UE_ROOT`, `EDITOR_CMD`, `BUILD_BAT`, `PROJECT` | Fixed engine and project paths. `UnrealEditor-Cmd.exe` is the console build of the editor binary: it logs to stdout and returns a process exit code, which the GUI `UnrealEditor.exe` does not do reliably |
| `performance_core_mask()` | Copied from P01 (repository UE5-UpdateBudget). Asks Windows for every physical core's `EfficiencyClass` (`GetLogicalProcessorInformationEx`, `RelationProcessorCore`) and ORs together the masks of the highest class. On this i7-14650HX it returns `0xFFFF` |
| `server_and_client_masks()` | Server = P-cores; clients = all remaining logical CPUs (`everything & ~p_mask` = `0xFF0000`, `04` §6). On a non-hybrid CPU both get everything, and the manifest shows it |
| `KERNEL32` setup | Loads `kernel32.dll` once and declares the argument and return types of the affinity calls, so 64-bit handles and masks are not truncated (`04` §7) |
| `pin_self(mask)` | Copied from P01: `SetProcessAffinityMask(GetCurrentProcess(), mask)` on **this script**. A process started afterwards inherits the mask from its first instruction, engine start-up included |
| `applied_mask()` | Copied from P01: reads the mask back from Windows (`GetProcessAffinityMask`), so the manifest records what was applied, not what was requested |
| `launch_pinned(mask, exe, args)` | `pin_self(mask)`, read the applied mask back, then start the **real** executable with `subprocess.Popen([exe] + args)` (a list, so Python quotes paths with spaces correctly) and return the process plus the mask it inherited. The server is started with the P-core mask, then the script re-pins itself and starts the clients with the E-core mask. *Rejected:* `cmd /c start /affinity`, which always returns 0 and hides crashes (`07` F9); and pinning the child after it starts, which leaves its first instructions unpinned |
| `restore_full_affinity()` | Gives the script all CPUs back after a run, in the caller's `finally` block |
| `kill_tree(process)` | `taskkill /PID … /T /F`: ends the process and any children, then waits |
| `working_sets_mb(image)` | Reads `tasklist /FO CSV` and returns the working set of every process with that image name, in MB. No extra Python packages needed |
| `sanitize(text)` | Replaces the user-profile folder with `%USERPROFILE%` before a manifest is written, so committed files hold no personal paths |
| `wait_for_text(path, needle, timeout, count)` | Polls a log until a line appears `count` times; prints a heartbeat every 10 s so the queue's idle watchdog sees progress |

### B2. `Tools\probe_net.py`: what can the launcher engine run?

| Piece | What it does, and why |
|---|---|
| `SETUPS` | Three replication setups and the arguments that select them (`legacy_push`, `repgraph`, `iris`), each with console commands the server prints at start-up as evidence (`Net.IsPushModelEnabled`, `net.Iris.UseIrisReplication`, `net.Iris.PushModelMode`, `Net.RepGraph.PrintGraph`) |
| `EVIDENCE` | Regular expressions for the log lines that prove a fact: "IpNetDriver listening on port", "using replication model Generic/BasicReplicationGraph/Iris", "Ensure condition failed: …", "Bringing … up for play (max tick rate N)", "Join succeeded", "Welcomed by server", "Cmdline -UseIrisReplication=1 is forcing …", and fatal errors. Each was found in the engine source first (`World.cpp:6008, 7605`, `PendingNetGame.cpp:379`, `IpNetDriver.cpp:1034`, `NetDriver.cpp:1939`, `UnrealEngine.cpp:14868`). The replication-model and ensure patterns were added after P2-1, where those lines were read by hand (`09`). The "Cmdline … is forcing" line never appeared in P2-1, although Iris was selected; the replication-model line is the reliable evidence |
| `probe_server_target()` | Runs UnrealBuildTool on `NetCostLabServer` and stores the exit code and the refusal line |
| `probe_setup()` | Starts the editor binary with `-server` (P-core mask), waits until it listens, starts two headless clients (E-core mask), waits until both are joined and welcomed, waits 15 s, samples working sets and checks that no process exited early, then kills everything and restores the script's affinity in a `finally` block. The mask each process inherited is recorded |
| `--client packaged`, `--setups`, `--skip-server-target`, `--label` | Phase 4 check F4 reuses the same script with the **packaged** client exe and no UnrealBuildTool step |
| The manifest | Written to `Benchmarks\Raw\<batch>_manifest.json`, passed through `sanitize()`; the full logs go to a git-ignored sub-folder |

### B3. `Tools\foundation_job.py`: the probe and every Phase 4 check in one queue job

| Piece | What it does, and why |
|---|---|
| `STEPS` | Six steps in dependency order: build the editor target (F1/F2), the Phase 2 probe (P2-1), build the game target (F1), package (F3a), the packaged smoke run (F3b), and the packaged-client check (F4). Each step lists the steps it needs; if one of those failed, it is skipped and recorded as skipped |
| Why one job | Every step needs the machine to itself and the queue serves ten projects; one job keeps the order fixed and avoids six waits in line. The probe runs after the editor build because the `.uproject` now lists a C++ module, and the editor binary refuses to start a project whose module DLLs are missing |
| `run_step()` | Streams every child line to the job's own output (the queue's idle watchdog sees progress through long cooks) and to `Benchmarks\Raw\<batch>\<step>.log`; counts lines that look like warnings or errors (ignoring "0 error(s)" summaries) and keeps up to 12 samples |
| The manifest | `<batch>_manifest.json` with each step's command, exit code, duration and counts, passed through `sanitize()`. The exit code is 1 if any step failed, so the queue marks the job as failed |
| `cmd /c <script>.bat` | Batch files run through `cmd.exe`; the list form of `Popen` quotes the path (it contains a space) |

### B4. The probe's `NetCostLabServer.Target.cs` (deleted in F1, kept here for the record)

```csharp
public class NetCostLabServerTarget : TargetRules
{
	public NetCostLabServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
	}
}
```

`TargetType.Server` is the only line that matters: it asks for a dedicated-server executable. It had no `ExtraModuleNames` on purpose: UnrealBuildTool checks the installed platform list right after compiling the rules (`UEBuildTarget.cs:1365–1404`), before it looks at any module, so the probe needed nothing else.


---

## Part C · Step S1: the workload

The workload is everything that replicates. It must be **identical in every configuration**, so it lives entirely on the server, depends on nothing but the seed and the server's clock, and never asks which replication system is running.

### C1. `NetCostLab.Build.cs` (update) and `NCLLog.h`

| Line | What it does, and why |
|---|---|
| `PrivateDependencyModuleNames.AddRange(new string[] { "NetCore" });` | `MARK_PROPERTY_DIRTY_FROM_NAME` lives in `Net/Core/PushModel/PushModel.h`, in the `NetCore` module. Only our `.cpp` files include it, so the dependency is **private** (`06` §2). `Iris`/`IrisCore` are still not needed: nothing in S1 talks to Iris |
| `DECLARE_LOG_CATEGORY_EXTERN(LogNetCostLab, Log, All);` | One log category for the whole game module, declared in a header and defined once in the module `.cpp` (`DEFINE_LOG_CATEGORY(LogNetCostLab);`). `Log` is the default verbosity, `All` the most verbose level that can be compiled in. *Why not `DEFINE_LOG_CATEGORY_STATIC` per file (as P01 did):* every class of this module logs to the same category, so the check scripts can count "our" errors with one pattern (`LogNetCostLab: Error:`) |

### C2. `NCLDrone.h` / `.cpp`: the drone

```cpp
USTRUCT()
struct FNCLDroneState
{
	GENERATED_BODY()
	UPROPERTY() uint16 Seq = 0;
	UPROPERTY() uint8 Mode = 0;
	UPROPERTY() int8 TargetSlot = -1;
	UPROPERTY() int16 Health = 100;
};
```

| Line | What it does, and why |
|---|---|
| `USTRUCT()` + `GENERATED_BODY()` | Makes the struct known to the reflection system, so it can be a replicated `UPROPERTY` (`06` §6). Without reflection the replication systems could not see its members |
| `UPROPERTY() uint16 Seq` | Each member must be a `UPROPERTY` too, or it is not replicated. `Seq` counts gameplay changes (step S3 uses it to match a client's arrival to the server change). `uint16` (65,535) is plenty: one change per 2 s is 36 changes per minute |
| `uint8 Mode`, `int8 TargetSlot`, `int16 Health` | The smallest integer types that hold the values: the legacy property serializer and Iris's quantisers both send fewer bits for smaller types, and every configuration pays the same. `int8` holds −1 ("nobody") and slots 0–127 |
| Why one struct | `Seq`, `Mode`, `TargetSlot` and `Health` always change together; as one property they always arrive together, so a client can never see a new `Seq` with an old `Mode` |

```cpp
ANCLDrone::ANCLDrone()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bReplicates = true;
	SetReplicatingMovement(true);
	SetNetUpdateFrequency(30.0f);
	SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance));
}
```

| Line | What it does, and why |
|---|---|
| `PrimaryActorTick.bCanEverTick = false` | The drone never ticks. 1,000 actor ticks would cost game-thread time that has nothing to do with replication and would differ from nothing; the workload subsystem moves all drones in one loop instead |
| `RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"))` | An actor needs a root scene component to have a location. A bare `USceneComponent` has no mesh, no collision and no render state: nothing to cost the server. *Rejected:* a static mesh (only the S8 demo client draws anything) |
| `bReplicates = true` | The actor replicates. Set in the constructor, so the class default already replicates and every system sees it from spawn |
| `SetReplicatingMovement(true)` | Replicates the actor's position (`ReplicatedMovement`, push-based in the engine, `ActorReplication.cpp:580–581`). `SetReplicatingMovement` only sets the flag ("without causing other side effects", `Actor.h:4556–4557`), which is what a constructor wants; `SetReplicateMovement` (`:766`) also updates live replication state |
| `SetNetUpdateFrequency(30.0f)` | Considered for replication up to 30 times per second: the server's tick rate, because drones move every tick. The public `NetUpdateFrequency` field is deprecated since 5.5 (`Actor.h:903`); the setter is the supported way |
| `SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance))` | 150 m relevancy, the engine default (`Actor.cpp:312`), written out so all three systems cull at the same distance (`01b` #11). The value lives once, in `NCLNet.h` (`inline constexpr float CullDistance = 15000.0f`, `04` §8), and drones, props and avatars all use it: three literal copies could drift apart (found by the S1 simplify pass). Squared, because distance checks compare squared lengths and skip a square root |

```cpp
FDoRepLifetimeParams PushBased;
PushBased.bIsPushBased = true;
DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, State, PushBased);
...
const FDoRepLifetimeParams InitialOnly{COND_InitialOnly, REPNOTIFY_OnChanged, /*bIsPushBased*/ true};
DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, DroneIndex, InitialOnly);
```

| Line | What it does, and why |
|---|---|
| `Super::GetLifetimeReplicatedProps(OutLifetimeProps)` | Keeps `AActor`'s own replicated properties (movement, role, owner…). Forgetting it silently stops movement replication |
| `bIsPushBased = true` | Registers the property for push model (`UnrealNetwork.h:134–151`): with `Net.IsPushModelEnabled=1` the server compares it only after `MARK_PROPERTY_DIRTY…` (C2–C6); with push off (C1) it is compared at every update like any other property. The same registration serves every configuration |
| `DOREPLIFETIME_WITH_PARAMS_FAST` | Registers one replicated property with its parameters. `_FAST` uses the class's generated `ENetFields_Private` enum instead of a name lookup (`UnrealNetwork.h:231`). Every property tagged `Replicated` must be registered, or UnrealHeaderTool's check at start-up reports it |
| `COND_InitialOnly` for `DroneIndex` | Sent with the drone's first replication only. The index never changes, so later updates never include or compare it |
| `REPNOTIFY_OnChanged` | The default notify rule; no `OnRep` exists for these properties in S1 |

```cpp
void ANCLDrone::ApplyStateChange(uint8 NewMode, int8 NewTargetSlot, int16 NewHealth)
{
	++State.Seq;
	State.Mode = NewMode; State.TargetSlot = NewTargetSlot; State.Health = NewHealth;
	MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, State, this);
}
```

| Line | What it does, and why |
|---|---|
| `++State.Seq` | Every change gets a new sequence number, even if the values happen to repeat |
| `MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, State, this)` | Tells push model "this property changed" (`PushModel.h:454`). In C1 (push off) it does nothing useful; its cost is the same in every configuration. Forgetting it in a push configuration is the classic push-model bug: the server never sends the change (`07` S1-2) |
| `ApplyLoadoutChange` | Four separate properties, four separate dirty marks: changing one field marks only that one. They change every 30 s; between changes a polling server compares them for nothing (H1) |

### C3. `NCLProp.h` / `.cpp`

Same pattern as the drone, with three differences:
- **no movement replication** and no `DroneIndex`: props never move and are not tracked per object by the correctness measurement;
- `SetNetUpdateFrequency(5.0f)`: a competent setting for something that changes every 20 s; 30 Hz would be a strawman for the legacy systems;
- `ApplyToggle(int8 NewOwnerSlot)` changes all four fields and marks each dirty. The values follow simple rules (state flips, charges count down and refill, cooldown follows the state) so the change is real data, not a constant.

### C4. `NCLPlayerAvatar.h` / `.cpp`

| Line | What it does, and why |
|---|---|
| `class ANCLPlayerAvatar : public AActor` | **An actor, not a pawn** (design change in S1, `02` §13). A possessed pawn is an *autonomous proxy* on its own client, and `ReplicatedMovement` is `COND_SimulatedOrPhysics` (`ActorReplication.cpp:580`), so the player's own client would never receive its avatar's position; possession also adds controller/pawn replication the study does not need. The replication views only need a **view target** (`NetConnection.cpp:861–865`) |
| `SetReplicatingMovement(true)`, `SetNetUpdateFrequency(30.0f)`, 150 m cull distance | Like a drone: it moves every tick |
| `UPROPERTY(Replicated) int8 PlayerSlot` + `COND_InitialOnly` | Which player this is, sent once. Drones refer to players by slot (C2) |

### C5. `NCLPlayerController.h` / `.cpp`

```cpp
void ANCLPlayerController::GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	if (HasAuthority())
	{
		if (const AActor* Target = GetViewTarget(); Target && Target != this)
		{
			OutLocation = Target->GetActorLocation();
			OutRotation = Target->GetActorRotation();
			return;
		}
	}
	Super::GetPlayerViewPoint(OutLocation, OutRotation);
}
```

| Line | What it does, and why |
|---|---|
| `override` of `GetPlayerViewPoint` | Every replication system asks this function where a connection is looking (`01b` #12). The engine's version answers from the camera cache that the **client** keeps updating (`PlayerController.cpp:1001–1031`); ours answers from the server's own view target, so the view is identical in every configuration and every run |
| `HasAuthority()` | Only the server's copy of the controller changes behaviour. On a client (the S8 demo camera) the engine's normal logic still runs |
| `if (const AActor* Target = GetViewTarget(); Target && Target != this)` | C++17 `if` with an initialiser (`04` §9): the variable exists only inside the `if`. `Target != this`: a controller with no view target set views itself; then the engine's answer is used |
| `Super::GetPlayerViewPoint(...)` | The fallback keeps the engine's behaviour for every other case |
| `OutRotation = Target->GetActorRotation()` | The view direction matters: legacy priority multiplies an actor behind the viewer by 0.2 or 0.4 (by distance) and one nearly straight ahead by 2 (`ActorReplication.cpp:67–88`). The workload turns each avatar to face its direction of travel every tick (C7, `LoopFacing`), so the view looks where the player walks. Before the S1 review the avatars never turned, and every view looked along +X |

### C6. `NCLGameMode.h` / `.cpp`

| Line | What it does, and why |
|---|---|
| `PlayerControllerClass = ANCLPlayerController::StaticClass();` | Every joining player gets our controller (C5) |
| `DefaultPawnClass = nullptr;` | No pawn class: players never get a pawn |
| `PostLogin(APlayerController* NewPlayer)` | Called on the server once a player has joined (`GameModeBase.h:326`). `Super` first (the engine's bookkeeping), then the workload spawns an avatar and `SetViewTarget(Avatar)` makes it the player's view (`PlayerController.h:2154`) |
| `Logout(AController* Exiting)` | Frees the avatar before `Super` forgets the player (`GameModeBase.h:342`) |
| `RestartPlayer(AController*) override {}` | The engine calls this to (re)spawn a player's pawn (`GameModeBase.h:435`). With no pawn class it would try and fail; the empty override says plainly that there is nothing to spawn. The parameter name is commented out because it is unused |
| `GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>()` | World subsystems are found by class from the world. It can be null in a world type the subsystem does not support, hence the `if` |
| `Config\DefaultEngine.ini`: `GlobalDefaultGameMode` and `GlobalDefaultServerGameMode = /Script/NetCostLab.NCLGameMode` | The game mode used when the map does not name one (`GameMapsSettings.h:216–223`); the engine template map does not (the probe logs showed the default `GameModeBase`). The server line is for dedicated servers; both are set so the editor's PIE and the server behave alike |

### C7. `NCLWorkloadSubsystem.h` / `.cpp`: the workload

**The shape.** A `UTickableWorldSubsystem` (`06` §7): one per game world, ticked once per frame after the actor ticks. `Initialize` reads the command line, `OnWorldBeginPlay` spawns everything on the server, `Tick` moves and changes it.

| Piece | What it does, and why |
|---|---|
| `namespace NCLWorkload { constexpr … }` | Every workload number in one place, named, with its reason: a 200 m arena; drone loops 5–30 m at 4–6 m/s; a state change every 2 s per drone with a 25% chance to attack; loadout every 30 s; props every 20 s; avatars 20–80 m loops at 3 m/s. `constexpr` makes them compile-time constants (`04` §8) |
| `struct FLoop` | A circle: centre, radius, angular speed (sign = direction), phase. Public, so tests can build one |
| `struct FSettings` + `ReadSettings()`, called from `Initialize` | The command-line values (`-NCLSeed=`, `-NCLDrones=`, `-NCLProps=`, `-NCLCountLog=`), read once with `FParse::Value` (`06` §10). Drones are clamped to `MAX_int16`, because `DroneIndex` is an `int16`. Read in `Initialize` (when the world creates its subsystems), not at begin play: in a standalone or listen-server world the host's player logs in, and `AddPlayer` needs the seed, before the world begins play (`UnrealEngine.cpp:16646` vs `:16660`; found by the S1 review) |
| `static FPlan MakePlan(const FSettings&)` | **Pure:** from the seed alone it produces every drone loop, every prop position and the workload hash. No world, no actors, so the automation tests (C8) can call it. `FRandomStream Placement(Seed)` (`06` §10) gives the same numbers for the same seed on every machine and build |
| `Loop.AngularSpeed = Direction * Speed / Loop.Radius` | Speed along the circle = angular speed × radius, so dividing turns the drawn speed (cm/s) into rad/s. Half the drones turn the other way |
| `CentreLimit = ArenaHalfExtent - Loop.Radius` | The whole circle stays inside the arena; the test checks it |
| Props: `GridColumns = ceil(sqrt(N))`, cell centre plus ±1.5 m jitter | An even spread over the arena (28 × 28 cells for 768 props), not perfectly regular |
| `FCrc::TypeCrc32(…, Hash)` | Folds every generated number into one 32-bit CRC. Equal hashes mean equal workloads (SC1); logged as `NCL workload: … hash=0x…` |
| `OnWorldBeginPlay` → `SpawnWorkload` only if `GetNetMode() != NM_Client` | Clients get the actors by replication; spawning them there too would duplicate them. Runs before any actor's `BeginPlay` (P01 verified the order), so the workload exists before the first player joins |
| `SpawnActor<ANCLDrone>(…, FTransform(LoopPosition(Loop, Now)), SpawnParams)` | Spawned directly at its loop position. `AlwaysSpawn` skips the collision check (the actors have no collision, and a failed spawn would change the workload) |
| `Drone->SetDroneIndex(Index)` right after spawn | Before the drone's first replication, so the initial-only property is in its first bunch |
| `enum class EStream` + `static MakeStream(Seed, Kind, Index, Number)` | **One random stream per event.** The stream's seed is a CRC of the run's seed, the kind of event (drone state, drone loadout, prop toggle, avatar loop), the object's index and the event's number (0, 1, 2, …). The values of drone 17's sixth state change are then the same in every run and configuration, whatever happened before it in the tick. The first version kept one stateful stream per drone, shared by its state and loadout changes, and one stream for all props; the S1 review showed that two changes falling due in the same tick in one run and in two ticks in another drew in a different order and diverged from there (`07` S1-4). The CRC also replaces `Seed * 7919 + Index`, which overflowed `int32` (undefined behaviour) for seeds above about 271,000 |
| `struct FSchedule { NextTime, Count }` | One recurring change of one object: when it is next due and how many times it has happened (the `Number` of its next stream) |
| `StateSchedules.Add({Now + Period × (Index + 0.5) / N, 0})` | Spreads the drones evenly over each 2 s period: about 4 changes per tick instead of 256 at once every 2 s. A burst would mix "change cost" into the per-tick measurement. `{…, 0}` is aggregate initialisation (`04` §13) |
| `ApplyDroneChange(Drone, Index, Number)`: three draws (`FRand`, `FRand`, `RandRange`) | Even when the drone does not attack, the target fraction is still drawn, so the same draw answers the same question in every run; the fraction is turned into a player slot (`OccupiedSlotAt`) only for an attack. The slot is a fraction of the players connected **now**: it is the same in every configuration once all N players have joined (the warm-up). The drone comes in as a reference: the `Tick` loop already read it from `Drones` once |
| `OccupiedSlotAt(float Fraction)` | Picks the k-th occupied player slot, k = ⌊Fraction × occupied⌋. Uniform over the players actually connected; −1 if none |
| `Tick`: `SetActorLocation(LoopPosition(Loop, Now))` | Position is a function of the server's world time, not an accumulated sum: no drift, and the S3 measurement can recompute the true position for any instant |
| `static LoopFacing(Loop, Time)` | The derivative of `LoopPosition`, (−ω sin θ, ω cos θ, 0), turned into a yaw with `FVector::ToOrientationRotator()`: the direction of travel, for either sign of ω. Avatars use `SetActorLocationAndRotation(LoopPosition, LoopFacing)` |
| `if (const int32 Number = TakeDue(Schedule, Now, Period); Number != INDEX_NONE)` | `TakeDue` returns the change's number and moves `NextTime` one period on **from its due time**, so the schedule keeps its phase. At most one change per object per tick: after a hitch an object that missed k changes catches up over k ticks instead of applying them at once. What it does **not** prevent (the S1 review corrected the comment): after a 1 s hitch about 30 ticks' worth of drones are all due, so the next tick applies about 128 changes. That burst is real and the server CSV shows it; S2 flags long frames |
| `AddPlayer`: the lowest free slot, `MAX_int8` limit | Slots are reused after a player leaves, so slot numbers stay small. `int8` caps the study at 128 players, far above the 32 we run |
| `MakeAvatarLoop(Seed, Slot)` | Its own stream per slot (`MakeStream(Seed, AvatarLoop, Slot, 0)`): player 7's path is the same in an 8-player and a 32-player run |
| `AvatarOwners` as `TWeakObjectPtr<AController>` | We need to find a leaving player's slot, but the controller belongs to the game mode; a weak pointer never keeps it alive and becomes null if it is destroyed (`06` §9) |
| `LogCounts()` with `TActorIterator<ANCLDrone>` | Counts the actors that exist in **this** world (on a client: what has arrived by replication). Only with `-NCLCountLog=SEC`, so normal runs pay nothing |
| `UPROPERTY(Transient) TArray<TObjectPtr<…>>` | The garbage collector sees these references (`06` §8). `Transient`: never saved |
| `GetStatId()` with `RETURN_QUICK_DECLARE_CYCLE_STAT` | Every tickable object must name a stat for the profiler (`Tickable.h:125`) |
| `DoesSupportWorldType`: Game and PIE | Never in the editor's own world, where nothing should spawn |

### C8. `Private\Tests\NCLWorkloadTests.cpp`

| Test | What it checks |
|---|---|
| `NetCostLab.Workload.PlanIsDeterministic` | Seed 1 twice gives the same hash, bit-identical loops (`FLoop`'s defaulted `==`, `04` §15) and identical prop positions; seed 2 gives a different hash; 256 drones and 768 props |
| `NetCostLab.Workload.PlanStaysInArena` | No drone loop leaves the 200 m arena; every drone speed is within 4–6 m/s; no prop is outside; 32 avatar loops are deterministic and inside |
| `NetCostLab.Workload.LoopMotionMatchesSpeed` | `LoopPosition` starts at the phase angle, keeps the centre's height, moves 5 m/s over one 30 Hz tick, and returns to the start after one period. `LoopFacing` points along the motion (dot product > 0.9999 with the next 0.1 ms step) for both directions of travel, with no pitch or roll |
| `NetCostLab.Workload.ChangeStreamsArePure` | `MakeStream` with the same key gives the same first three values; changing any one of seed, kind, object or change number changes them; `MAX_int32` as the seed works |
| `NetCostLab.Workload.ScheduleKeepsPhase` | `TakeDue`: not due before its time; the first change is number 0; the next time is a period after the due time, not after "now"; never twice in one tick; after a 5 s hitch, the two missed changes come on two consecutive ticks; the count is 3 |

`IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Name", Flags)` declares a test class and registers it under its dotted name (`06` §11); `Flags` = editor and client contexts, one filter flag. `TestTrue` is used for the `uint32` hash comparisons because `TestEqual` has `int32`, `int64` and `SIZE_T` overloads and a `uint32` would be ambiguous (`AutomationTest.h:1985–1987`). Tests run headless in the editor binary through `Tools\run_tests.bat`.

### C9. Tools added or changed in S1

| File | What changed, and why |
|---|---|
| `Tools\run_tests.bat [Filter] [LogPath]` | Runs `Automation RunTests <Filter>; Quit` in the editor binary, headless. The log goes to `LogPath`, **relative to `Tools\`** (default `..\NetCostLab\Saved\Logs\Tests.log`). Relative, because `cmd /c` strips the first and last quote of a command line that holds more than one quoted argument, and every absolute path here contains the space of `UNREAL PORTFOLIO` (`07` S1-15). The editor's own exit code is the verdict: non-zero if no test matched or any test reported an error (`AutomationCommandline.cpp:346–355, 493–504`). Adapted from P01 |
| `Tools\check_job.py` | One queue job per Phase 5 step: `--build`, `--tests FILTER`, `--package`, `--smoke`, `--probe "ARGS"` (repeatable), always in that order, stopping at the first failure. Reuses `foundation_job.run_step`, `TOOLS` and `PY`, and `netlab_env`'s batch helpers. After the S1 review: the test step passes `run_tests.bat` a log path inside the fresh batch folder (`tests_editor.log`, given relative to `Tools\`), so an old run's log can never be read, and records each test's result in the manifest (from the editor's "Test Completed. Result={…}" lines); `--probe` strings are split by `split_args` (whitespace outside double quotes), because `shlex` would eat the backslashes of a Windows path |
| `Tools\probe_net.py` | New setup `legacy` (C1, push off); `--extra=ARGS` (added to server and clients; an `--extra` value starting with `-` must use the `=` form); `--hold SEC`; evidence patterns for our `NCL workload`, `NCL player joined/left` and `NCL counts` lines (the last four count lines are kept: the pattern is one constant, `COUNTS`, used in both lists). The switches shared by server and clients are built once (`tail`) |
| `Tools\netlab_env.py` (S1 simplify pass) | `ENGINE` (the engine string every manifest records), `RAW_DIR`, `new_batch(label)` (batch name and its git-ignored folder) and `write_manifest(batch, manifest)` (sanitised JSON). `foundation_job.py`, `probe_net.py` and `check_job.py` each had their own copy of these lines |

---

## Part D · Step S2: the director, the telemetry and the run script

The director runs one benchmark run from inside the game: it decides when the measured window opens and closes, measures it, and ends every process. Everything that measures lives here, so the workload stays identical whether or not a run is measured.

### D1. `NCLGameState.h` / `.cpp`: the run phase, replicated

| Line | What it does, and why |
|---|---|
| `UENUM() enum class ENCLBenchPhase : uint8 { Waiting, WarmUp, Capture, Settle, Dump }` | The five phases of a run, one byte on the wire. `UENUM` makes it a reflected type, so it can be a replicated property |
| `class ANCLGameState : public AGameStateBase` | The game state exists once per world and is replicated to every client. `AGameStateBase` matches our `AGameModeBase` (mixing the two families logs an error, `06` §23) |
| `UPROPERTY(Replicated) ENCLBenchPhase BenchPhase` + `DOREPLIFETIME_WITH_PARAMS_FAST(…, PushBased)` | The phase replicates push-based, like every other property in this project (`06` §12–13) |
| `SetBenchPhase`: assign, `MARK_PROPERTY_DIRTY_FROM_NAME`, `ForceNetUpdate()` | Mark it dirty for the push model, and ask to be considered at the next net update (`ForceNetUpdate`). The game state keeps the actor default of 100 Hz (`Actor.cpp:295`), so today it is considered every 30 Hz tick anyway; the call keeps the phase prompt in every system if that rate is ever lowered |
| Always relevant | `AGameStateBase` sets `bAlwaysRelevant` (`GameStateBase.cpp:26`); Iris does not spatially filter `AInfo` actors (`BaseEngine.ini:1519`). Every client gets the phase in every configuration |

`ANCLGameMode` sets `GameStateClass = ANCLGameState::StaticClass()`.

### D2. `NCLBenchSubsystem.h` / `.cpp`: the director

**The shape.** Another `UTickableWorldSubsystem`, on the server and on every client. `Initialize` reads the command line; `OnWorldBeginPlay` decides the role; `Tick` runs the phase machine; `OnWorldTickEnd` measures.

| Piece | What it does, and why |
|---|---|
| `FSettings` + `ReadSettings()` | `-NCLClients=N` (server: how many to wait for; 0 = the director stays idle, so probes and smoke runs are unaffected), `-NCLWarmup=SEC` (20), `-NCLCaptureFrames=N` (1,800 = 60 s at 30 Hz), `-NCLSettle=SEC` (5), `-NCLRunTag`, `-NCLConfig` (a label in the CSV metadata and the report), `-NCLOutDir`, `-NCLClientIndex=K` (a client's report number), `-NCLClientFPS=N` (a client's frame cap), `-NCLExit` |
| Client: `GEngine->SetMaxFPS(ClientMaxFPS)` in `OnWorldBeginPlay` | The clients' 30 fps cap. *Why not `-dpcvars=t.MaxFPS=30`:* the first S2 run showed clients at about 2,100 fps with exactly that switch. `UGameEngine::Init` re-applies the user settings on every process that is not a dedicated server and sets `t.MaxFPS` to `FrameRateLimit`, which is 0 (`GameEngine.cpp:1249–1250`, `GameUserSettings.cpp:535–538`); `SetMaxFPS` keeps the previous priority, so the switch's value is simply replaced (`UnrealEngine.cpp:12247–12254`). A client's game world begins play after that, so the cap set here stays (`07` S2-8) |
| `namespace NCLBench`: `ClientReportTimeoutSeconds` 30, `LongFrameTicks` 2; `ViewedAvatar(Controller)`, `WorkloadHashText(World)` | The director's constants, named, and two file-local helpers used in several places. A frame longer than two net ticks (`1 / NetServerMaxTickRate`, read when the window opens) is a hitch: counted in the report, never hidden |
| `DoesSupportWorldType`: `Game` only | In PIE several client worlds share one process and one command line, so their reports would overwrite each other; measured runs never use PIE |
| `OnWorldBeginPlay`: `bServer`, `bDirecting`, `OnWorldTickEnd.AddUObject` | A server directs only with `-NCLClients`; a client always follows (idle, it costs one pointer check per frame). Only a directing server binds the tick-end delegate; `Deinitialize` removes it |
| `TickServer`: `Waiting` → `WarmUp` | Waits until `CountJoinedPlayers()` (connections whose controller's view target is an avatar, i.e. `PostLogin` has run) reaches N |
| `WarmUp` → `Capture`: `BeginMeasuredWindow()` | Lists the connections, writes CSV metadata (`NCLRunTag`, `NCLConfig`, `NCLClients`, `NCLWorkloadHash`) and calls `FCsvProfiler::BeginCapture(-1, OutDir, RunTag + "_server.csv")`. The capture starts at the **next** frame (`CsvProfiler.cpp:3870–3895`) |
| `OnWorldTickEnd`, first call in `Capture`: `TakeBaseline()` | The frame that requested the capture is not captured, so it is not counted either: its tick end records each connection's `OutTotalBytes` and the engine's saturated-frame count as starting points and starts the wall clock |
| Every later call: `SampleFrame()` | One captured frame. Frame 1 emits `CSV_EVENT(BenchStart)` (only now is the capture running, `07` S2-1). Per connection: bytes this frame (`OutTotalBytes` delta, after the flush, `06` §20–21) and whether the frame was saturated: the engine itself counts, per connection, the frames that end with the connection not ready to send (`SaturationAnalytics.TrackFrame(!IsNetReady())`, `NetConnection.cpp:5110`), which is the metric `01` §6.4 defines; the director reads that counter and its step this frame (`07` S2-4). Per frame: four CSV columns, `NetCostLab/BusyMsPrevFrame` (the previous frame's delta minus its idle sleep, `UnrealEngine.cpp:3156`), `/OutBytes`, `/SatConnections`, `/Clients` |
| After frame N: `EndMeasuredWindow()` | `CSV_EVENT(BenchEnd)`, `EndCapture()` (this frame is still captured, `CsvProfiler.cpp:4208–4218`), freezes the workload's changes, enters `Settle`. The CSV and the byte window therefore cover **the same N frames** (`07` S2-2) |
| `Settle` → `Dump`: `WriteServerReport()` | After 5 s without changes, `<run>_server.json`: tag, config, hash, window seconds and frames, long frames, saturated connection-frames (the sum over connections), and per connection its slot, bytes, bytes per second and saturated frames (`FJsonObject`, serialised in `SaveJson`, `06` §22) |
| `Dump`: `RequestExit` when every client has left and the CSV future is ready | Each client leaves after writing its report, so an empty `ClientConnections` means every report is on disk. A 30 s timeout covers a client that never sees `Dump` (heavy loss); the server never quits before the asynchronous CSV write has finished. The log says which of the two ended the run |
| `TickClient` | Watches the replicated phase (on a client, `Phase` holds the phase last seen). Entering `Capture`: records `InTotalBytes` and the clock; leaving it: the end values; counts frames in between. On `Dump`: writes `<run>_client<K>.json` (bytes, bytes per second, frames, fps, its slot), stops directing (so the report is written once) and quits with `-NCLExit` |
| `FreezeChanges()` (in `UNCLWorkloadSubsystem`) | The workload keeps moving but applies no more changes: the settle period lets the last ones arrive before S3 compares state |

### D3. Tests

S2 adds no C++ automation test: the director has no pure logic left once saturation comes from the engine's own counter (an earlier draft had a `NetCostLab.Bench.SaturationRule` test for a self-made rule; both went with the S2 simplify pass, `07` S2-4). The director is checked by the S2 runs themselves (`09` S2), and `csv_stats.py --selftest` checks the statistics.

### D4. Tools added or changed in S2

| File | What changed, and why |
|---|---|
| `Tools\run_bench.py` | The batch runner (adapted from P01's). `CONFIGS` holds C1–C6 as data: Iris on/off for both sides, the server's cvars (`-dpcvars=`), config overrides (`-ini:Engine:`) and arguments. `network_args` sets the rate in its three places (client request, server maximum and internet maximum, `01b` #9) and adds `-PktLoss=` only when loss is asked for (parsed at net-driver start, `NetDriver.cpp:788`, `NetEmulationHelper.cpp:547`). A run: pin and start the server, wait for "listening", pin and start N packaged clients, wait for the server to exit by itself, give the clients 30 s, kill anything left in `finally`, then `collect`. Interleaving: one discarded priming run, then the config order rotates every round. Valid = no failure, server and every client exited with 0, every report readable (`read_json`; an unreadable one makes the run invalid instead of crashing the batch), the CSV holds exactly `--capture-frames` frames, and every client ran between 27.3 and 33 fps (the cap held, and no client starved). Clients get `-NCLClientFPS=30`. The manifest is rewritten after every run, so a crash later keeps the runs already done. The environment record reads the charger state from `root/wmi BatteryStatus.PowerOnline` and the toolchain of the last build from UnrealBuildTool's log. `--dry-run` prints the commands and writes nothing |
| `Tools\csv_stats.py` | P01's script adapted: the P05 columns by default, `NetFlushMs` derived per frame as `ServerReplicateActors` + `NetworkOutgoing` (`row_sums`; a frame counts only if both cells exist), the `nclruntag` metadata key, the P05 event names in the self-test, and the event-split option removed (P05 captures contain only the window). The EVENTS column is looked up once per capture (`events_index`). A partial capture (no metadata row) drops any header row and unreadable cells instead of failing |
| `Tools\check_job.py` | `--bench "ARGS"` (repeatable): `run_bench.py` inside the same queue job, after the probes (one loop over both kinds) |
| `Tools\netlab_env.py` | `MAP`, `PACKAGED_CLIENT`, `COMMON_ARGS`, `IPNETDRIVER` and `REPGRAPH_INI` now live here, shared by `probe_net.py` and `run_bench.py` |
