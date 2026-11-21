# 06 · Unreal APIs And Macros: NetCostLab

**Rule:** only APIs this project actually uses, each in the same eight points: 1 what it is · 2 why it exists · 3 how it works · 4 when to use it · 5 when not to · 6 common mistakes · 7 a small example · 8 how this project uses it. Every engine claim is checked in the UE 5.8.4 source. Current to **Phase 5, step S2**; later steps add their APIs.

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

---

## APIs added in step S1 (the workload)

## 6. `UCLASS` / `USTRUCT` / `UPROPERTY` / `GENERATED_BODY`

1. **What:** markers that UnrealHeaderTool reads before the C++ compiler runs; it generates the reflection code (`*.generated.h`) that `GENERATED_BODY()` pulls in.
2. **Why:** Unreal's garbage collector, serialisation, Blueprint and **replication** all work from reflection data, not from C++ types.
3. **How:** `UCLASS()` / `USTRUCT()` register the type; each `UPROPERTY()` member gets a property descriptor; `Replicated` / `ReplicatedUsing=` mark it for replication. Fixed-width integers (`uint8`, `int8`, `uint16`, `int16`, `int32`) are all supported property types.
4. **When:** any type or member the engine must see: replicated fields, object references the GC must follow.
5. **When not:** plain data the engine never needs to see (our `FLoop`, `FPlan`): no reflection, no generated code.
6. **Mistakes:** a member without `UPROPERTY()` inside a replicated struct is silently not replicated; a `Replicated` property not registered in `GetLifetimeReplicatedProps` is reported at start-up.
7. **Example:** `UPROPERTY(Replicated) int32 Ammo = 30;`
8. **Here:** `FNCLDroneState` (one replicated struct), the drone/prop/avatar fields, and the subsystem's `TArray<TObjectPtr<…>>` arrays.

## 7. `UWorldSubsystem` / `UTickableWorldSubsystem`

1. **What:** an object the engine creates automatically for each world, with the world's lifetime; the tickable variant also ticks once per frame.
2. **Why:** world-wide systems without a placed manager actor.
3. **How:** `DoesSupportWorldType` (`Subsystems/WorldSubsystem.h:66`) filters world types; `OnWorldBeginPlay(UWorld&)` (`:43`) runs before any actor's `BeginPlay`; the tick is "conditional" (`IsTickable()`, `Tickable.h:114`) and runs after the actor tick groups (P01 `01b` §2); `GetStatId` is pure virtual (`WorldSubsystem.h:92`).
4. **When:** one coordinating object per world (our workload, later the director).
5. **When not:** per-actor behaviour (use components) or state that must outlive a world (game-instance subsystem).
6. **Mistakes:** assuming `Super::Tick` does work (it only asserts the subsystem is initialised, `WorldSubsystem.cpp:89–92`; we call it anyway, as the base class expects); spawning in `Initialize` instead of `OnWorldBeginPlay` (the world is not ready); the opposite for settings: reading them only in `OnWorldBeginPlay`, although in a standalone or listen-server world the host player logs in before the world begins play (`UnrealEngine.cpp:16646` vs `:16660`); so `Initialize(FSubsystemCollectionBase&)` (`WorldSubsystem.h:96`, call `Super` first) reads the command line and `OnWorldBeginPlay` spawns; creating it in editor worlds.
7. **Example:** `GetWorld()->GetSubsystem<UNCLWorkloadSubsystem>()`.
8. **Here:** `UNCLWorkloadSubsystem`; the game mode finds it by class.

## 8. `TObjectPtr` and `UPROPERTY` object references

1. **What:** `TObjectPtr<T>` is the engine's wrapper for a pointer to a UObject held in a `UPROPERTY`.
2. **Why:** the garbage collector only follows references it can see through reflection.
3. **How:** a `UPROPERTY() TArray<TObjectPtr<ANCLDrone>>` is a GC root path: the drones are not collected while the array holds them; a destroyed actor's entry becomes invalid, which `IsValid()` detects.
4. **When:** every UObject pointer stored in a UObject member.
5. **When not:** local variables and function parameters (raw pointers are fine there).
6. **Mistakes:** storing UObject pointers in a non-`UPROPERTY` container (they dangle after GC); using a destroyed actor without `IsValid()`.
7. **Example:** `UPROPERTY(Transient) TArray<TObjectPtr<ANCLDrone>> Drones;`
8. **Here:** the workload's `Drones`, `Props`, `Avatars`; every use checks `IsValid`.

## 9. `TWeakObjectPtr`

1. **What:** a pointer to a UObject that does **not** keep it alive and turns null when it is destroyed.
2. **Why:** to remember an object owned by someone else.
3. **How:** stores an object index and serial number; `Get()` returns null once the object is gone.
4. **When:** back-references to objects whose lifetime we do not own (the game mode owns player controllers).
5. **When not:** objects we own and must keep (use `TObjectPtr` in a `UPROPERTY`).
6. **Mistakes:** comparing it to a raw pointer without `Get()`; assuming it is still valid one frame later.
7. **Example:** `TWeakObjectPtr<AController> Owner = Player; if (Owner.Get() == Exiting) …`
8. **Here:** `UNCLWorkloadSubsystem::AvatarOwners`.

## 10. `FRandomStream`, `FParse::Value`, `FCrc::TypeCrc32`

1. **What:** a seeded pseudo-random generator; a command-line value parser; a CRC-32 over any trivially copyable value.
2. **Why:** reproducible workloads, run settings from the command line, and a cheap identity check for "same workload".
3. **How:** `FRandomStream(int32 Seed)` (`Math/RandomStream.h:40`) gives the same sequence on every machine; `FRand()`, `RandRange(Min, Max)` and `FRandRange(Min, Max)` are `const` (the state is `mutable`); `FParse::Value(CommandLine, TEXT("NCLSeed="), Out)` leaves `Out` unchanged if the key is missing; `FCrc::TypeCrc32(Value, Crc)` (`Misc/Crc.h:36`) folds a value's bytes into a running CRC.
4. **When:** anything that must be identical between runs.
5. **When not:** `FMath::Rand()` (global state, shared with everything else) for workload decisions.
6. **Mistakes:** one shared stream for everything (a change in one consumer shifts every other); even one stateful stream per object, if two kinds of change share it: when both fall due in one tick in one run and in two ticks in another, they draw in a different order (the S1 review found this in our first version); a different number of draws on different paths (our drone change always draws three values); deriving a seed by multiplying `int32`s (`Seed * 104729` overflows, which is undefined behaviour).
7. **Example:** `const FRandomStream Stream = MakeStream(Seed, EStream::AvatarLoop, Slot, 0); const double R = Stream.FRandRange(2000.0, 8000.0);` where `MakeStream` seeds the stream with a CRC chain of (seed, kind, index, event number).
8. **Here:** `MakePlan` (one placement stream); one stream **per event** for drone state and loadout changes, prop toggles and avatar loops (`MakeStream`); the workload hash.

## 11. Automation tests: `IMPLEMENT_SIMPLE_AUTOMATION_TEST`

1. **What:** a macro that declares and registers a test class with a dotted name.
2. **Why:** tests run in the engine (headless) and report pass/fail to the command line.
3. **How:** `IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class, "Name", Flags)` (`Misc/AutomationTest.h:4297`); `RunTest` uses `TestTrue`/`TestEqual`; flags need exactly one filter flag (`ProductFilter`); `WITH_DEV_AUTOMATION_TESTS` compiles them out of Shipping.
4. **When:** anything checkable without a running network game.
5. **When not:** multi-process network behaviour (that is `probe_net.py`).
6. **Mistakes:** two filter flags (registration fails); `TestEqual` with a type that has no exact overload (`uint32`: ambiguous).
7. **Example:** `TestTrue(TEXT("same seed, same hash"), First.Hash == Second.Hash);`
8. **Here:** `NetCostLab.Workload.*`, run by `Tools\run_tests.bat`.

## 12. Replication registration: `GetLifetimeReplicatedProps`, `DOREPLIFETIME_WITH_PARAMS_FAST`, `FDoRepLifetimeParams`

1. **What:** the per-class list of replicated properties and how each replicates.
2. **Why:** the property flag says *that* a property replicates; this says *when* (condition), *how* (push-based?) and whether `OnRep` fires.
3. **How:** `FDoRepLifetimeParams { Condition = COND_None; RepNotifyCondition = REPNOTIFY_OnChanged; bIsPushBased = false; }` (`Net/UnrealNetwork.h:134–151`); `COND_InitialOnly` sends a property in the first bunch only; `_FAST` uses the generated `ENetFields_Private` enum (`:231`). Both legacy and Iris read this list (Iris builds its replication state descriptors from it).
4. **When:** every replicated class.
5. **When not:** —
6. **Mistakes:** forgetting `Super::GetLifetimeReplicatedProps` (the actor's movement stops replicating); registering a property push-based and never marking it dirty (it never updates with push on).
7. **Example:** `DOREPLIFETIME_WITH_PARAMS_FAST(ANCLDrone, State, PushBased);`
8. **Here:** drone, prop and avatar; everything push-based; indices and slots initial-only.

## 13. Push model: `MARK_PROPERTY_DIRTY_FROM_NAME`

1. **What:** tells the replication system that one property of one object changed.
2. **Why:** so a push-model server compares only what changed (`01b` #2, #6).
3. **How:** `MARK_PROPERTY_DIRTY_FROM_NAME(Class, Property, Object)` (`Net/Core/PushModel/PushModel.h:454`) marks the property's rep index dirty for the object's push id; with `WITH_PUSH_MODEL=0` it compiles to nothing (`:480–489`); at run time it matters only if `Net.IsPushModelEnabled` is on; Iris picks the marks up through its legacy push-model bridge (`LegacyPushModel.cpp`).
4. **When:** after every change to a push-based property, on the server.
5. **When not:** on clients; for properties not registered push-based.
6. **Mistakes:** changing a field without marking it (silent missing updates, `07` S1-2); marking from a const function through a cast.
7. **Example:** `Ammo = NewAmmo; MARK_PROPERTY_DIRTY_FROM_NAME(ANCLDrone, Ammo, this);`
8. **Here:** every setter of the drone, prop and avatar.

## 14. Actor replication setters: `SetReplicatingMovement`, `SetNetUpdateFrequency`, `SetNetCullDistanceSquared`

1. **What:** the per-actor knobs: replicate the position, how often to consider the actor, how far away it stays relevant.
2. **Why:** they decide how much each actor costs to replicate.
3. **How:** `SetReplicatingMovement` only sets the flag (`Actor.h:4556–4557`); `SetNetUpdateFrequency` (`:4624`) replaces the deprecated public field (`:903`); `SetNetCullDistanceSquared` (`:4648`) takes the squared distance. All three systems read them (Iris and Replication Graph from the class default object, `01b` #11).
4. **When:** in the constructor, so the class default already has them.
5. **When not:** per frame (Replication Graph reads class defaults once: a run-time change would not reach it).
6. **Mistakes:** passing a distance instead of its square; a 100 Hz default update rate on rarely changing actors.
7. **Example:** `SetNetCullDistanceSquared(FMath::Square(NCLNet::CullDistance));` (15000 cm, one constant in `NCLNet.h`)
8. **Here:** drones and avatars 30 Hz with movement, props 5 Hz without; 150 m everywhere.

## 15. Game-mode and controller hooks: `PostLogin`, `Logout`, `RestartPlayer`, `SetViewTarget`, `GetPlayerViewPoint`

1. **What:** the server's player life-cycle callbacks, and where a player looks.
2. **Why:** to give each player an avatar and make it what the replication systems treat as the player's view.
3. **How:** `PostLogin` (`GameFramework/GameModeBase.h:326`) after a player joins; `Logout` (`:342`) when it leaves; `RestartPlayer` (`:435`) is what spawns pawns; `SetViewTarget` (`PlayerController.h:2154`); `GetPlayerViewPoint` is virtual and is called by every system's view code (`01b` #12).
4. **When:** server-side player management.
5. **When not:** —
6. **Mistakes:** forgetting `Super::PostLogin` (the engine's own join handling is skipped); leaving the default pawn class and getting a pawn per player anyway.
7. **Example:** `NewPlayer->SetViewTarget(Avatar);`
8. **Here:** `ANCLGameMode` and `ANCLPlayerController`.

## 16. `UWorld::SpawnActor` with `FActorSpawnParameters`

1. **What:** creates an actor in a world at a transform.
2. **Why:** the workload is spawned by code.
3. **How:** `SpawnActor<T>(Class, Transform, Params)`; `SpawnCollisionHandlingOverride = AlwaysSpawn` skips the collision test; on a server, a replicated actor starts replicating as it begins play (`Actor.cpp:4768`).
4. **When:** run-time actors.
5. **When not:** —
6. **Mistakes:** spawning on clients what the server also spawns (duplicates); relying on a collision-checked spawn that can fail and change the workload.
7. **Example:** `World.SpawnActor<ANCLProp>(ANCLProp::StaticClass(), FTransform(Position), SpawnParams);` and, with a rotation, `FTransform(Rotation, Location)`.
8. **Here:** drones, props and avatars, server only. Avatars then move with `SetActorLocationAndRotation` every tick (location and facing in one call, one transform update).

## 17. Log categories: `DECLARE_LOG_CATEGORY_EXTERN` / `DEFINE_LOG_CATEGORY` / `UE_LOG`

1. **What:** a named log channel with a default verbosity.
2. **Why:** our lines are recognisable and countable in every log.
3. **How:** declared in a header, defined in one `.cpp`; `UE_LOG(LogNetCostLab, Display, TEXT("…"), …)`.
4. **When:** module-wide logging.
5. **When not:** per-frame logging in measured runs (it costs time).
6. **Mistakes:** defining the category in a header (multiple definitions).
7. **Example:** `UE_LOG(LogNetCostLab, Display, TEXT("NCL player joined: slot %d"), Slot);`
8. **Here:** `NCLLog.h` / `NetCostLabModule.cpp`; the probe counts `LogNetCostLab: Error:` lines.

## 18. `TActorIterator`

1. **What:** iterates every actor of a class in a world (`EngineUtils.h`).
2. **Why:** to count what has arrived on a client without keeping our own lists there.
3. **How:** `for (TActorIterator<ANCLDrone> It(World); It; ++It)`; it walks the world's actor list, so it is O(actors).
4. **When:** occasional diagnostics.
5. **When not:** every frame in a measured run.
6. **Mistakes:** using it per frame on large worlds.
7. **Example:** see `UNCLWorkloadSubsystem::LogCounts`.
8. **Here:** the optional `-NCLCountLog=SEC` counts.

## 19. The CSV profiler from code: `FCsvProfiler::BeginCapture` / `EndCapture`, `CSV_DEFINE_CATEGORY`, `CSV_CUSTOM_STAT`, `CSV_EVENT`, `CSV_METADATA`

1. **What:** the engine's per-frame CSV recorder, driven from C++: start and stop a capture, add our own columns, mark frames with events, and attach key-value metadata to the file.
2. **Why:** the server must open the measured window itself, at the moment its own phase logic decides, and the file must carry our per-frame numbers next to the engine's timers.
3. **How:**
   - `FCsvProfiler::Get()->BeginCapture(NumFrames, Folder, Filename, Flags)` (`ProfilingDebugging/CsvProfiler.h:519–522`) only **queues** a start command; it takes effect at the start of the next frame (`CsvProfiler.cpp:3870–3895`).
   - `EndCapture()` (`:529`) queues a stop that is processed at the end of the current frame, which is still captured (`CsvProfiler.cpp:4208–4218`); the file is written asynchronously, and the returned `TSharedFuture<FString>` becomes ready with its name.
   - Without the `CompressOutput` flag the file is a plain `.csv` (`csv.CompressionMode` default −1, `CsvProfiler.cpp:184–190`); the folder and the file name are used as given, the extension is set to `.csv` (`:3973–3976`).
   - `CSV_DEFINE_CATEGORY(NetCostLab, true)` in one `.cpp` creates the category, enabled by default; `CSV_CUSTOM_STAT(NetCostLab, OutBytes, Value, ECsvCustomStatOp::Set)` writes the column `NetCostLab/OutBytes` for this frame.
   - `CSV_EVENT(NetCostLab, TEXT("BenchStart"))` is recorded only while a capture runs (`RecordEventfInternal`, `CsvProfiler.cpp:4744–4746`).
   - `CSV_METADATA(Key, Value)` (`CsvProfiler.h:85`) ends up in the file's last row (lower-cased keys in `csv_stats.py`).
   - Everything compiles to nothing where `CSV_PROFILER` is 0 (Shipping, `CsvProfilerConfig.h:31`).
4. **When:** a measured window that code decides; per-frame values of our own.
5. **When not:** `-ExitAfterCsvProfiling` would end the process when the capture ends, before the settle and the state dump (`01` §7.1). Per-frame `UE_LOG` instead of a CSV column (costly and hard to parse).
6. **Mistakes:** emitting the start event in the same frame as `BeginCapture` (it is dropped: the capture is not running yet); quitting before the asynchronous write has finished; assuming the CSV and a hand-counted window cover the same frames without checking.
7. **Example:** `FCsvProfiler::Get()->BeginCapture(-1, OutDir, RunTag + TEXT("_server.csv"));`
8. **Here:** `UNCLBenchSubsystem`: `BeginCapture` when the warm-up ends, `BenchStart` in the first captured frame, four `NetCostLab/…` columns per frame, `EndCapture` after exactly `-NCLCaptureFrames` frames, and the server quits only when the future is ready.

## 20. `FWorldDelegates::OnWorldTickEnd`

1. **What:** a global multicast delegate broadcast at the very end of `UWorld::Tick` (`Engine/World.h:4519–4520`).
2. **Why:** it runs **after** the net driver's flush (`LevelTick.cpp:1929` then `:2061`), so the bytes each connection sent this frame are already counted.
3. **How:** `AddUObject(this, &UClass::Function)` returns an `FDelegateHandle`; the signature is `(UWorld*, ELevelTick, float)`. It fires for every world, so the handler checks `InWorld == GetWorld()`.
4. **When:** per-frame sampling of what the frame did.
5. **When not:** anything that must happen before replication (use a normal tick).
6. **Mistakes:** forgetting `Remove(Handle)` in `Deinitialize` (a dangling binding after the world is gone); not filtering by world (PIE and multiple worlds).
7. **Example:** `TickEndHandle = FWorldDelegates::OnWorldTickEnd.AddUObject(this, &UNCLBenchSubsystem::OnWorldTickEnd);`
8. **Here:** the director's frame-by-frame measured window: baseline, per-connection bytes, saturation, busy ms, and the end of the window.

## 21. `UNetConnection` counters: `OutTotalBytes`, `InTotalBytes`, `GetSaturationAnalytics()`; `UNetDriver::ClientConnections` / `ServerConnection`

1. **What:** each connection's running totals of bytes sent and received (`int32`, `Engine/NetConnection.h:571`), and the engine's own saturation statistics per connection (`GetSaturationAnalytics()`, `NetConnection.h:1624`, an `FNetConnectionSaturationAnalytics`, `Net/NetAnalyticsTypes.h:217–290`).
2. **Why:** bytes per second per client, and how often a connection was saturated, with no tracing.
3. **How:** `OutTotalBytes += PacketBytes` for every packet flushed (`NetConnection.cpp:2586`), headers included; never reset. At the end of every connection tick the engine calls `SaturationAnalytics.TrackFrame(!IsNetReady())` (`NetConnection.cpp:5110`), unconditionally and in every replication system, so `GetNumberOfSaturatedFrames()` counts the frames that ended with the connection over its bandwidth budget. Read it at two points and subtract; `ResetSaturationAnalytics()` exists, but resetting engine state is not needed. The server's connections are `NetDriver->ClientConnections`; a client's single connection is `NetDriver->ServerConnection`.
4. **When:** per-connection measurements at a defined point in the frame.
5. **When not:** as an exact payload size: it counts whole packets, so headers and acks are included (the same in every configuration).
6. **Mistakes:** sampling before the flush (you count last frame's packets); writing your own saturation rule from bytes and rate when the engine already counts `IsNetReady() == false` per connection (our first S2 version did, `07` S2-4).
7. **Example:** `const int64 Sent = Connection->OutTotalBytes - Sample.BytesLastFrame;`
8. **Here:** the server's per-connection samples and the clients' received bytes.

## 22. JSON output: `FJsonObject`, `FJsonValueObject`, `FJsonSerializer`, `TJsonWriterFactory`

1. **What:** the engine's JSON DOM and writer (module `Json`).
2. **Why:** the run reports are read by Python; hand-built `Printf` JSON breaks on a backslash in a path or a quote in a name.
3. **How:** `MakeShared<FJsonObject>()`, then `SetStringField`, `SetNumberField`, `SetArrayField` (an array of `TSharedPtr<FJsonValue>`, e.g. `MakeShared<FJsonValueObject>(Entry)`); `FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&OutString))`. Numbers are doubles.
4. **When:** files read by tools.
5. **When not:** per-frame logging in a measured window (allocation); the reports are written in the Settle/Dump phases, after the window.
6. **Mistakes:** forgetting the `Json` module dependency (link errors); storing a 64-bit count beyond 2^53 as a double (not an issue for bytes per run).
7. **Example:** `Report->SetNumberField(TEXT("window_frames"), WindowFrames);`
8. **Here:** `<run>_server.json` and `<run>_clientNN.json`.

## 23. Game state as a replicated channel for run phases: `GameStateClass`, `AGameStateBase`, `ForceNetUpdate`

1. **What:** the game mode's `GameStateClass` picks the game-state actor every world spawns; `AGameStateBase` is always relevant (`bAlwaysRelevant = true`, `GameStateBase.cpp:26`), and Iris does not spatially filter `AInfo` actors (`BaseEngine.ini:1519`).
2. **Why:** headless clients need to know when the measured window starts and ends, with nothing but replication.
3. **How:** a push-based `UPROPERTY(Replicated)` phase; the setter marks it dirty and calls `ForceNetUpdate()` so it is considered on the next net update instead of waiting for the actor's update interval.
4. **When:** a small amount of world-wide state every client needs.
5. **When not:** per-player data (player state) or anything frequent.
6. **Mistakes:** a game mode derived from `AGameModeBase` with a game state derived from `AGameState` (or the reverse): the engine logs an error ("Mixing AGameStateBase with AGameMode is not compatible", `GameMode.cpp:68`; the reverse at `GameState.cpp:31`) and the game state's match logic does not work.
7. **Example:** `GameStateClass = ANCLGameState::StaticClass();`
8. **Here:** `ANCLGameState::BenchPhase`, read by the client side of the director.

## 24. `FPlatformMisc::RequestExit`

1. **What:** asks the engine to shut down at the end of the frame (`RequestExit(bForce, CallSite)`).
2. **Why:** every process of a run must end by itself, so the run script sees real exit codes and nothing is left running.
3. **How:** with `bForce = false`, the engine loop ends normally and the process exits with 0 unless an error was flagged.
4. **When:** a process that has finished its job.
5. **When not:** to abort on an error (log an error instead, so the run is marked invalid).
6. **Mistakes:** exiting before asynchronous writes (the CSV) finish.
7. **Example:** `FPlatformMisc::RequestExit(false, TEXT("UNCLBenchSubsystem"));`
8. **Here:** the server after Dump, once every client has left (or after a 30 s timeout) and the CSV future is ready; each client after writing its report.
