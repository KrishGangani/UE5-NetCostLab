# 05 · Line By Line Code Explanation: NetCostLab

**Rule:** every file that exists in `NetCostLab\` and `Tools\` is explained here, line by line, in the same step it is written. Sections follow the build order in [03_Implementation_Roadmap.md](03_Implementation_Roadmap.md).

| Part | Step | Files |
|---|---|---|
| A | Phase 4, F1–F3 (foundation) | `.uproject`, two `Target.cs`, two `Build.cs`, two module `.cpp`, `.uplugin`, config `.ini` files, `Tools\*.bat` |
| B | Phase 2 probe and Phase 4 check F4 | `Tools\netlab_env.py`, `Tools\probe_net.py`, `Tools\foundation_job.py`, the removed probe `NetCostLabServer.Target.cs` |

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
