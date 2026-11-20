# 06 · Unreal APIs And Macros: NetCostLab

**Rule:** only APIs this project actually uses, each in the same eight points: 1 what it is · 2 why it exists · 3 how it works · 4 when to use it · 5 when not to · 6 common mistakes · 7 a small example · 8 how this project uses it. Every engine claim is checked in the UE 5.8.4 source. Current to **Phase 4**; Phase 5 adds the replication, Iris and gameplay APIs step by step.

| # | API | First used in |
|---|---|---|
| 1 | `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE` | Phase 4 (F1, F2) |
| 2 | `ModuleRules`: dependencies, `PCHUsage`, `SetupIrisSupport` | Phase 4 |
| 3 | `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`, and the settings a launcher engine will not let you change | Phase 4 |
| 4 | Project and plugin descriptors: modules, `LoadingPhase`, plugin dependencies | Phase 4 |
| 5 | Command-line switches for networked runs: `-server`, `-game`, `-nullrhi`, `-UseIrisReplication=`, `-dpcvars=`, `-ini:`, `-ExecCmds=`, `-abslog=`, `-traceautostart=0` | Phase 2 probe, Phase 4 (F4) |

---

## 1. `IMPLEMENT_PRIMARY_GAME_MODULE` / `IMPLEMENT_MODULE`

1. **What:** macros that turn a C++ module into something the engine can load. `IMPLEMENT_PRIMARY_GAME_MODULE` is for the project's main game module; `IMPLEMENT_MODULE` for every other module (our plugin).
2. **Why:** the module manager needs a uniform entry point per module, in both DLL (editor) and monolithic (packaged) builds.
3. **How:** in modular builds an `InitializeModule` function is generated and called after the DLL loads; in monolithic builds a static registrant object registers the module at start-up (`Core/Public/Modules/ModuleManager.h:943–960`). The primary version also defines `GInternalProjectName` (from `UE_PROJECT_NAME`) and `GIsGameAgnosticExe = false` in monolithic builds (`:1100–1136`); its third argument is named `DEPRECATED_GameName` and is ignored ("manually specifying a name is no longer necessary").
4. **When to use:** once per module, in one `.cpp` file.
5. **When not to:** never twice in one module, and never `IMPLEMENT_PRIMARY_GAME_MODULE` in a plugin.
6. **Common mistakes:** a module name that does not match the folder, the `Build.cs` class and the `.uproject` entry (the module then fails to load, or a compile-time warning fires in monolithic builds); writing a custom module class with empty `StartupModule`/`ShutdownModule` when `FDefaultModuleImpl` already is exactly that.
7. **Example:** `IMPLEMENT_MODULE(FDefaultModuleImpl, MyPlugin);`
8. **In this project:** `NetCostLabModule.cpp` uses `IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, NetCostLab, "NetCostLab")`; `NetCostLabCoreModule.cpp` uses `IMPLEMENT_MODULE(FDefaultModuleImpl, NetCostLabCore)`. Neither module needs start-up logic.

## 2. `ModuleRules`: dependencies, `PCHUsage`, `SetupIrisSupport`

1. **What:** the C# class in a module's `*.Build.cs` that tells UnrealBuildTool how to compile the module.
2. **Why:** each module declares which other modules it uses, so include paths, link libraries and load order follow from one list.
3. **How:**
   - `PublicDependencyModuleNames`: modules whose headers **our public headers** include; anything depending on us inherits them.
   - `PrivateDependencyModuleNames`: modules only our `.cpp` files and private headers use.
   - `PCHUsage = UseExplicitOrSharedPCHs`: use the engine's shared precompiled header unless the module names its own.
   - `SetupIrisSupport(Target, bAddAsPublicDependency = false)` (`Programs/UnrealBuildTool/Configuration/Rules/ModuleRules.cs:1747–1762`): adds `UE_NET_HAS_IRIS_FASTARRAY_BINDING=1` (private), `UE_WITH_IRIS=1` (public) and an `IrisCore` dependency (private by default). The engine's own `Engine.Build.cs:514–515` calls it with a public dependency.
4. **When to use:** every module. `SetupIrisSupport` in any module that includes Iris headers.
5. **When not to:** don't add a dependency "just in case"; each one costs build time and couples modules.
6. **Common mistakes:** making everything public (F8 in `07`); adding `IrisCore` to the dependency list by hand without `SetupIrisSupport`, which misses the defines the Iris headers expect; forgetting that the class name must equal the module name.
7. **Example:** `PrivateDependencyModuleNames.AddRange(new string[] { "NetCore" }); SetupIrisSupport(Target);`
8. **In this project:** the game module depends on `Core`, `CoreUObject`, `Engine` (public) in Phase 4; replication modules are added in S1 when code uses them. The plugin `NetCostLabCore` calls `SetupIrisSupport(Target)` from Phase 4, because its only purpose is an Iris prioritiser, and the first build already proves the Iris headers and defines resolve against the installed engine.

## 3. `TargetRules`: `Type`, `DefaultBuildSettings`, `IncludeOrderVersion`, and what a launcher engine locks

1. **What:** the C# class in `*.Target.cs` describing one executable to build.
2. **Why:** the editor and a packaged game are different programs with different module sets and defines.
3. **How:**
   - `Type`: `Game`, `Editor`, `Client`, `Server` or `Program`.
   - `DefaultBuildSettings = BuildSettingsVersion.V7` and `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8`: the values Epic's 5.8 `TP_Blank` template uses (`Templates/TP_Blank/Source/TP_Blank.Target.cs`).
   - Some properties carry `[RequiresUniqueBuildEnvironment]`, for example `bWithPushModel` (`Rules/TargetRules.cs:1516–1527`, default `Type == TargetType.Editor`) and `bWithServerCode` (`:1495–1501`). Changing one in a shared-environment target is refused (`UEBuildTarget.cs:1509–1517`), and a unique environment is refused on an installed engine (`Rules/RulesAssembly.cs:710–712`).
   - On an installed engine the target type itself must be listed in `[InstalledPlatforms]` (`UEBuildTarget.cs:1395–1418`); the 5.8.4 launcher engine lists Editor and Game only.
4. **When to use:** one file per executable type you need (here Game and Editor).
5. **When not to:** don't add a Server or Client target on a launcher engine; it cannot be built (F1 in `07`).
6. **Common mistakes:** setting `bWithPushModel = true` in a game target and expecting it to work (F2, F3); forgetting `ExtraModuleNames`, so the game module is never built.
7. **Example:** `Type = TargetType.Editor; ExtraModuleNames.Add("MyGame");`
8. **In this project:** `NetCostLab.Target.cs` (Game) builds the packaged headless clients; `NetCostLabEditor.Target.cs` builds the modules the editor binary loads, which is also our dedicated server. The Phase 2 probe's `NetCostLabServer.Target.cs` showed that a Server target is refused (`09`, P2-1) and was deleted in F1.

## 4. Project and plugin descriptors

1. **What:** JSON files: `NetCostLab.uproject` for the project, `NetCostLabCore.uplugin` for the plugin.
2. **Why:** they tell the engine and UnrealBuildTool which modules exist, when to load them, and which plugins to enable.
3. **How:**
   - `"Modules": [{ "Name", "Type": "Runtime", "LoadingPhase": "Default" }]`: a runtime module, loaded at the normal point, before the first world exists.
   - `"Plugins": [{ "Name": "Iris", "Enabled": true }]` in a `.uproject` enables an engine plugin for this project. A `.uplugin` may list the plugins it depends on in the same format (example: `Engine/Plugins/Runtime/ADM/ADM.uplugin:27–32`).
   - Engine plugins `Iris` (`Plugins/Experimental/Iris/Iris.uplugin`) and `ReplicationGraph` (`Plugins/Runtime/ReplicationGraph/ReplicationGraph.uplugin`) are both `EnabledByDefault: false` and `IsBetaVersion: true` in 5.8.4.
4. **When to use:** always; the descriptors are the project.
5. **When not to:** don't enable plugins you don't use: each adds modules to load and cook.
6. **Common mistakes:** relying on `EnabledByDefault` for your own plugin instead of listing it (the dependency becomes invisible); forgetting that the `Iris` plugin must be enabled for `-UseIrisReplication=1` to have any effect (F5).
7. **Example:** `{ "Name": "ReplicationGraph", "Enabled": true }`
8. **In this project:** the `.uproject` enables `NetCostLabCore`, `Iris` and `ReplicationGraph`, and **disables** the default-enabled `AndroidFileServer` (`"Enabled": false` turns off an `EnabledByDefault` engine plugin for this project), whose editor module would otherwise write a random security token into `DefaultEngine.ini` (`05` A1, `07` F13). The plugin declares `Iris` as a plugin dependency, because a prioritiser is useless without it.

## 5. Command-line switches for networked runs

1. **What:** arguments the engine reads at start-up.
2. **Why:** one binary, many configurations: every difference between C1 and C5 is a switch, visible in the manifest.
3. **How (each verified in 5.8.4):**

| Switch | Effect | Evidence |
|---|---|---|
| `-server` (editor binary) | Dedicated-server mode | `Core/Private/Misc/CoreMisc.cpp:250–262`; `Core/Public/Misc/CoreMisc.h:147–169` |
| `-game` (editor binary) | Run the project as a game, uncooked | `CoreMisc.h:171–193` |
| `-nullrhi` | No rendering device; nothing is drawn | Engine start-up (no window, no GPU) |
| `-UseIrisReplication=0\|1` | Forces the replication system, overriding config and cvar | `Net/Iris/Private/Iris/IrisConfig.cpp:33–43`; `Engine/Private/UnrealEngine.cpp:14865–14875` |
| `-dpcvars=Name=Value,...` | Sets console variables before device profiles apply | `Engine/Private/DeviceProfiles/DeviceProfileManager.cpp:737–738` |
| `-ini:Engine:[Section]:Key=Value` | Overrides one config value (non-Shipping) | `Core/Private/Misc/ConfigCacheIni.cpp:1714–1723`; `ConfigCacheIni.h:57` |
| `-ExecCmds=a,b` | Runs console commands after start-up | `Engine/Private/UnrealEngine.cpp:2552` |
| `-abslog=<path>` | Writes the log to an exact file | Used by P01's smoke runs; output checked in `09` |
| `-traceautostart=0` | An open Unreal Insights does not auto-trace the run | `Core/Private/ProfilingDebugging/TraceAuxiliary.cpp:267–268` (the flag), `:2378` (`FParse::Bool(CommandLine, TEXT("-traceautostart="), GTraceAutoStart)`) |

4. **When to use:** in scripts, so every run is reproducible from its command line.
5. **When not to:** never for settings that must be identical in every run; those belong in `DefaultEngine.ini`.
6. **Common mistakes:** giving `-UseIrisReplication` to only one side (F4); quoting: a value with spaces must be quoted for the Windows command line; cheat cvars cannot be set through `-dpcvars` (P01 `01b` #11).
7. **Example:** `UnrealEditor-Cmd.exe NetCostLab.uproject /Engine/Maps/Templates/Template_Default -server -UseIrisReplication=1 -dpcvars=Net.IsPushModelEnabled=1 -abslog=D:\logs\server.log`
8. **In this project:** `Tools\probe_net.py` (Phase 2 probe and Phase 4 check F4) and, from S2, `Tools\run_bench.py` build every command line from one configuration table.
