> **Raw Phase 2 research notes (A: launcher engine, server targets, push model availability), UE 5.8.4, 2026-11-21.** Kept as an evidence trail. Every claim cites a file:line under `C:\Program Files\Epic Games\UE_5.8\Engine` or a probe result. **The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md). Treat anything here that is not restated there as unverified.**

# P05 NetCostLab: Phase 2 research, part A: what the launcher engine can run

- `Build/Build.version`: `"MajorVersion": 5, "MinorVersion": 8, "PatchVersion": 4, "Changelist": 58902052`, `"BranchName": "++UE5+Release-5.8"`.
- `Build/InstalledBuild.txt` exists, so UnrealBuildTool treats this as an installed (launcher) engine.
- All paths below are relative to `Engine/`.

## A1. Which target types the installed engine accepts

`Config/BaseEngine.ini:3997–4026`, section `[InstalledPlatforms]`. For Win64 the only entries are:

```ini
+InstalledPlatformConfigurations=(PlatformName="Win64", Configuration="Development", PlatformType="Editor", ...)
+InstalledPlatformConfigurations=(PlatformName="Win64", Configuration="DebugGame",   PlatformType="Editor", ...)
+InstalledPlatformConfigurations=(PlatformName="Win64", Configuration="DebugGame",   PlatformType="Game", ...)
+InstalledPlatformConfigurations=(PlatformName="Win64", Configuration="Development", PlatformType="Game", ...)
+InstalledPlatformConfigurations=(PlatformName="Win64", Configuration="Shipping",    PlatformType="Game", ...)
```

- A count of `PlatformType="Server"` or `PlatformType="Client"` in the file returns **0**.
- `Binaries/Win64/` has `UnrealGame-*.target` receipts but no `UnrealServer*` receipt.

UnrealBuildTool's check, `Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:1395–1418` (inside `CreateRulesObject`, right after the rules assembly is compiled and **before** any module is compiled):

```csharp
if (!InstalledPlatformInfo.IsValid(RulesObject.Type, Descriptor.Platform, Descriptor.Configuration, EProjectType.Code, InstalledPlatformState.Downloaded) && Descriptor.Name != "UnrealHeaderTool")
{
    ...
    else if (!InstalledPlatformInfo.IsValid(RulesObject.Type, null, null, EProjectType.Code, InstalledPlatformState.Supported))
    {
        throw new BuildException("{0} targets are not currently supported from this engine distribution.", RulesObject.Type);
    }
    ...
    else
    {
        throw new BuildException("Targets cannot be built in the {0} configuration with this engine distribution.", RulesObject.Configuration);
    }
}
```

**[inferred]** With no Server entry at all, the second branch fires: "Server targets are not currently supported from this engine distribution." This is the same function that refused P01's Test configuration (last branch). **Checked by probe P2-1** (`09`, P2-1).

## A2. Push model is compiled only into Editor targets by default

`Source/Programs/UnrealBuildTool/Configuration/Rules/TargetRules.cs:1516–1527`:

```csharp
/// When enabled, Push Model Networking support will be compiled in.
/// This can help reduce CPU overhead of networking, at the cost of more memory.
/// Always enabled in editor builds.
[RequiresUniqueBuildEnvironment]
public bool bWithPushModel
{
    get => bWithPushModelOverride ?? (Type == TargetType.Editor);
    ...
```

`Source/Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:6573–6579`: `if (Rules.bWithPushModel) GlobalCompileEnvironment.Definitions.Add("WITH_PUSH_MODEL=1"); else ... ("WITH_PUSH_MODEL=0")`.

The engine's own game target `Source/UnrealGame.Target.cs`: `Type = TargetType.Game; BuildEnvironment = TargetBuildEnvironment.Shared;` and no `bWithPushModel` override, so the precompiled engine game modules were built with `WITH_PUSH_MODEL=0` **[inferred]**.

**Binary cross-check** (a byte search, ASCII and UTF-16, run 2026-11-21):

| File | `"IsPushModelEnabled"` | control string `"RPCDoSDetectionOverride"` (unguarded cvar in the same module) |
|---|---|---|
| `Intermediate/Build/Win64/x64/UnrealGame/Development/NetCore/Module.NetCore.cpp.obj` (precompiled game NetCore) | **0** hits | 8 ASCII + 2 UTF-16 hits |
| `Binaries/Win64/UnrealEditor-NetCore.dll` | 1 + 1 hits | 0 + 2 hits |

The cvar string lives in `Source/Runtime/Net/Core/Private/Net/Core/PushModel/PushModel.cpp:434–439` inside `#if WITH_PUSH_MODEL` (line 5). Its absence from the game object file, while the control string is present, confirms `WITH_PUSH_MODEL=0` in the precompiled game NetCore.

Why a project cannot just turn it on:
- `[RequiresUniqueBuildEnvironment]` on the property (`TargetRules.cs:1521`): changing it in a shared-environment target throws "...modifies the values of properties... Remove the modified setting, change ... to use a unique build environment ..., or set bOverrideBuildEnvironment = true" (`UEBuildTarget.cs:1509–1517`).
- A unique environment is refused on an installed engine: `RulesAssembly.cs:710–712`, "Targets with a unique build environment cannot be built with an installed engine."
- `bOverrideBuildEnvironment = true` would compile **our** modules with `WITH_PUSH_MODEL=1` against precompiled engine modules built with `=0`: the two sides would disagree on `PushModel.h` (`PushModel.h:7` vs `:480`) **[inferred: an ODR violation; not attempted]**.

What `WITH_PUSH_MODEL=0` does to game code (`Source/Runtime/Net/Core/Public/Net/Core/PushModel/PushModel.h:480–489`):

```cpp
#else // WITH_PUSH_MODEL
#define MARK_PROPERTY_DIRTY_UNSAFE(Object, RepIndex)
#define MARK_PROPERTY_DIRTY(Object, Property)
...
#define MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName, Object)
```

The macros expand to nothing and `Net.IsPushModelEnabled` does not exist, so **configuration C2 cannot exist in a packaged game**.

## A3. Which binary can be a dedicated server

`Source/Runtime/Core/Public/Misc/CoreMisc.h:147–169`:

```cpp
 * An editor build can be launched with -server to set this to true, but it will be false during single process PlayInEditor mode.
inline bool IsRunningDedicatedServer()
{
	if (FPlatformProperties::IsServerOnly()) { return true; }
	if (FPlatformProperties::IsGameOnly())   { return false; }
#if UE_EDITOR
	return (StaticDedicatedServerCheck() == 1);
#else
	return false;
#endif
}
```

`Source/Runtime/Core/Private/Misc/CoreMisc.cpp:250–262`: `StaticDedicatedServerCheck()` returns 1 when the command line has the switch `SERVER` or `RUN=SERVER`.

- A packaged **Game** binary (`IsGameOnly()`) is **never** a dedicated server.
- The **editor** binary with `-server` **is** one.
- A packaged game can still be a **listen** server (`?listen`), but then the host also runs a local player, and it has no push model (A2).

## A4. Tick rate of a dedicated server

`Source/Runtime/Engine/Private/GameEngine.cpp:1719–1745`: for `NM_DedicatedServer` (or a listen server with `bClampListenServerTickRate`), `MaxTickRate = FMath::Clamp(NetDriver->GetNetServerMaxTickRate(), 1, 1000)`.

`Config/BaseEngine.ini:1867`, `[/Script/OnlineSubsystemUtils.IpNetDriver]`: `NetServerMaxTickRate=30`.

`Source/Runtime/Engine/Private/UnrealEngine.cpp:3080–3157` (`UpdateTimeAndHandleMaxTickRate`): a dedicated server sleeps the remaining time with `FPlatformProcess::SleepNoStats(WaitTime)` (`:3094–3096`) and stores it with `FApp::SetIdleTime(ActualWaitTime)` (`:3156`). `FApp::GetIdleTime()` is public (`Core/Public/Misc/App.h:597`).

P01 (repository UE5-UpdateBudget, `01b` §4) verified that a fixed step (`-deterministic`) sets the delta to 1/FPS and **never waits** (`UnrealEngine.cpp:3015–3023`). A server in that mode would simulate faster than real time.

## A5. The game module needs Iris compiled in

- `Source/Runtime/Engine/Engine.Build.cs:514–515`: `SetupIrisSupport(Target, bIrisAddAsPublicDependency: true)`.
- `Source/Programs/UnrealBuildTool/Configuration/Rules/ModuleRules.cs:1747–1762`: `SetupIrisSupport` adds `UE_NET_HAS_IRIS_FASTARRAY_BINDING=1` (private), `UE_WITH_IRIS=1` (public) and a dependency on `IrisCore`.
- `Source/Runtime/Net/Iris/IrisCore.Build.cs`: `PublicDefinitions.Add("UE_WITH_IRIS=1")`; `UE_SUPPORT_PARALLEL_IRIS=1` unless the target is Client/Program or `bExcludeParallelIrisSupport`.
- Precompiled game intermediates exist for `IrisCore` (`Intermediate/Build/Win64/x64/UnrealGame/Development/IrisCore`), for the Iris plugin (`Plugins/Experimental/Iris/Intermediate/Build/Win64/x64/UnrealGame/Development/Iris`) and for ReplicationGraph. Editor DLLs: `UnrealEditor-IrisCore.dll`, `UnrealEditor-ReplicationGraph.dll`, `Plugins/Experimental/Iris/Binaries/Win64/UnrealEditor-Iris.dll`.

So Iris and Replication Graph are available in **both** editor and game builds; only push model is editor-only.

## A6. Plugins

| Plugin | Path | Flags (5.8.4) |
|---|---|---|
| Iris | `Plugins/Experimental/Iris/Iris.uplugin` | `"IsBetaVersion": true`, `"EnabledByDefault": false`, `"VersionName": "0.1"`. Its only module (`IrisModule.cpp:9–11`) loads `IrisCore` at startup |
| ReplicationGraph | `Plugins/Runtime/ReplicationGraph/ReplicationGraph.uplugin` | `"IsBetaVersion": true`, `"EnabledByDefault": false`, module `LoadingPhase: PreDefault` |

The Iris plugin folder is still named `Experimental` although the flag says Beta.
