> **Raw Phase 2 research notes (B: legacy, push model, Replication Graph, Iris and its prioritisers), UE 5.8.4, 2026-11-21.** Evidence trail. **The claims the design depends on are restated in [../01b_Research_Findings.md](../01b_Research_Findings.md); treat anything not restated there as unverified.** Paths are relative to `Engine/`.

# P05 NetCostLab: Phase 2 research, part B: the five configurations in source

## B1. Legacy (generic) replication and the priority hook

- `Source/Runtime/Engine/Classes/GameFramework/Actor.h:2835`: `ENGINE_API virtual float GetNetPriority(const FVector& ViewPos, const FVector& ViewDir, class AActor* Viewer, AActor* ViewTarget, UActorChannel* InChannel, float Time, bool bLowBandwidth);`
- `Source/Runtime/Engine/Private/NetDriver.cpp:5160` (legacy prioritisation): `Priority = FMath::Max<int32>(Priority, FMath::RoundToInt(65536.0f * ActorInfo->Actor->GetNetPriority(Viewers[i].ViewLocation, ...)));`
- So **legacy already allows gameplay-aware priority** per viewer by overriding a virtual. Replication Graph does not call it (`Plugins/Runtime/ReplicationGraph/Source/Public/ReplicationGraph.h:20`: "virtual functions like IsNetRelevantFor and GetNetPriority are not used by the replication graph"). Iris does not call it either (no match for `GetNetPriority` under `Source/Runtime/Net/Iris` or `Source/Runtime/Engine/Private/Net/Iris`).
- `net.UseAdaptiveNetUpdateFrequency` defaults to **0** (`NetDriver.cpp:523–526`).

## B2. Push model

- Cvar `Net.IsPushModelEnabled`, default **false**: `Source/Runtime/Net/Core/Private/Net/Core/PushModel/PushModel.cpp:434–439`, all inside `#if WITH_PUSH_MODEL` (`:5`).
- Macros: `PushModel.h:443` `MARK_PROPERTY_DIRTY(Object, Property)`, `:454` `MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName, Object)`; empty when `WITH_PUSH_MODEL=0` (`:480–489`).
- Opt-in per property: `Source/Runtime/Engine/Public/Net/UnrealNetwork.h:134–151` `struct FDoRepLifetimeParams { ELifetimeCondition Condition = COND_None; ELifetimeRepNotifyCondition RepNotifyCondition = REPNOTIFY_OnChanged; bool bIsPushBased = false; ... }`; `DOREPLIFETIME_WITH_PARAMS_FAST` at `:231`.
- Skipping whole replicators: `Source/Runtime/Engine/Private/DataReplication.cpp:75–79` `net.PushModelSkipUndirtiedReplication`, default **false**; `:81–84` `net.PushModelSkipUndirtiedFastArrays`, default false.
- **AActor's own replicated properties are already push-based** (`Source/Runtime/Engine/Private/ActorReplication.cpp:563–581`): `SharedParams.bIsPushBased = true` for `bReplicateMovement`, `Role`, `RemoteRole`, `Owner`, `bHidden`, `bTearOff`, `bCanBeDamaged`, `Instigator`; `AttachmentReplication` and `ReplicatedMovement` use `bUsePushModel = true` (`ReplicatedMovement`: `COND_SimulatedOrPhysics, REPNOTIFY_Always`).
- Tracker bugs to keep in mind (from [research/02](../Research/02_technical_challenges.md) [T3-C], snippet only): UE-191014 (push model stopping actors going dormant), UE-226689 (desync when a push property is not marked dirty across dormancy). The workload uses no dormancy.

## B3. Replication Graph

- `Source/Runtime/Engine/Classes/Engine/ReplicationDriver.h:10–22`: two ways to install a driver, `ReplicationDriverClassName` under `[/Script/OnlineSubsystemUtils.IpNetDriver]`, or binding `UReplicationDriver::CreateReplicationDriverDelegate()`.
- `NetDriver.h:849` `UPROPERTY(Config) FString ReplicationDriverClassName;`; `NetDriver.cpp:1820–1831` loads it; `:1870` `SetReplicationDriver(UReplicationDriver::CreateReplicationDriver(this, URL, GetWorld()))`.
- `NetDriver.cpp:8077–8111` `CreateReplicationDriver`: the delegate wins if bound; `Net.RepDriver.Enable` (default 1, `:8070–8071`) and `-RepDriverEnable/-RepDriverDisable`; logs "ReplicationDriverClass is null! Not using ReplicationDriver." when nothing is configured.
- **Cannot be combined with Iris:** `NetDriver.cpp:7354–7358` `if (IsUsingIrisReplication() && NewReplicationDriver) { checkf(false, TEXT("ReplicationDriver (%s) are not supported with a NetDriver (%s) configured to use Iris") ...`.
- `Plugins/Runtime/ReplicationGraph/Source/Public/BasicReplicationGraph.h`: "A basic implementation of replication graph. It only supports NetCullDistanceSquared, bAlwaysRelevant, bOnlyRelevantToOwner. These values cannot change per-actor at runtime. This is meant to provide a simple example implementation." Enable via `ReplicationDriverClassName="/Script/ReplicationGraph.BasicReplicationGraph"`.
- `Plugins/Runtime/ReplicationGraph/Source/Private/BasicReplicationGraph.cpp:42–43`: `ClassInfo.ReplicationPeriodFrame = GetReplicationPeriodFrameForFrequency(ActorCDO->GetNetUpdateFrequency());` (frame-based, from the server tick rate); `:47–51` cull distance from the CDO; `:62–72` one `UReplicationGraphNode_GridSpatialization2D` with `CellSize = 10000.f`.
- Console evidence command: `Net.RepGraph.PrintGraph` (`ReplicationGraph.cpp:937`).

## B4. Iris: how it is switched on in 5.8.4

1. **Plugin.** `Plugins/Experimental/Iris/Iris.uplugin`: `EnabledByDefault: false`, `IsBetaVersion: true`. Its module loads `IrisCore` (`IrisModule.cpp:9–11`).
2. **Per-driver permission.** `Config/BaseEngine.ini:346` `+IrisNetDriverConfigs=(NetDriverDefinition=GameNetDriver, bCanUseIris=true)`; `:347` DemoNetDriver false. Struct `FIrisNetDriverConfig` in `Engine/Classes/Engine/Engine.h:252–295` (`bCanUseIris`, `PreferredReplicationSystem`, `bCanUseParallelNetConnectionTick = false`).
3. **Global switch.** `Source/Runtime/Net/Iris/Private/Iris/IrisConfig.cpp:15–16` `net.Iris.UseIrisReplication`, default **0**, `ECVF_Default`. Command line `-UseIrisReplication=0|1` (`:33–43`) overrides everything.
4. **Decision order**, `Source/Runtime/Engine/Private/UnrealEngine.cpp:14800–14885` (`UEngine::WillNetDriverUseIris`): config `bCanUseIris` && cvar → `PreferredReplicationSystem` → `UGameInstance::GetDesiredReplicationSystem` → PIE follows the server → **command line last and strongest** (`:14865–14875`, logs "Cmdline -UseIrisReplication=1 is forcing NetDriver ..."). Then `ensureMsgf(IsModuleLoaded("IrisCore"), "...Check whether the Iris plugin is enabled.")` (`:14879–14885`).
5. **Server and client must agree.** `NetDriver.cpp:2725` (`bIsRemoteUsingIris && !IsUsingIrisReplication()`) and `:2755` (the reverse) handle a mismatch at connection time.
6. **Code against IrisCore** needs `SetupIrisSupport(Target)` in the module's `Build.cs` (part A5).
7. `DataStreamChannel` is already in `ChannelDefinitions` (`BaseEngine.ini:1845`), and `[/Script/IrisCore.ObjectReplicationBridgeConfig] RequiredNetDriverChannelClassName=/Script/Engine.DataStreamChannel` (`:1505–1506`).

## B5. Iris with and without push model

- `Source/Runtime/Net/Iris/Private/Iris/ReplicationSystem/LegacyPushModel.cpp:13–26` (`#if WITH_PUSH_MODEL`): `net.Iris.PushModelMode`, default **2**: "0 = disabled but runtime togglable, 1 = enabled and not togglable, 2 = enabled but runtime togglable. Requires Net.IsPushModelEnabled is true and WITH_PUSH_MODEL > 0".
- `LegacyPushModel.h:22` `IsIrisPushModelEnabled(bool bIsPushModelEnabled = IS_PUSH_MODEL_ENABLED()) { return (bIsIrisPushModelForceEnabled | (IrisPushModelMode > 0)) & bIsPushModelEnabled; }`; without `WITH_PUSH_MODEL`: `:96` `constexpr bool IsIrisPushModelEnabled(...) { return false; }`.
- `Polling/ObjectPoller.cpp:174–179, 230`: with Iris push off, objects are force-polled (`//If pushmodel is disabled, call ForcePollObject instead`).
- Consequence: **Iris in a packaged launcher game always polls**; Iris in the editor binary uses push dirtiness when `Net.IsPushModelEnabled=1`. C4 and C5 run with it on.

## B6. Iris defaults that matter for a fair comparison

`Config/BaseEngine.ini:1478–1535` (";Iris - begin"):

```ini
[/Script/IrisCore.NetObjectPrioritizerDefinitions]
+NetObjectPrioritizerDefinitions=(PrioritizerName=DefaultPrioritizer, ClassName=/Script/IrisCore.SphereNetObjectPrioritizer, ConfigClassName=/Script/IrisCore.SphereNetObjectPrioritizerConfig)
+NetObjectPrioritizerDefinitions=(PrioritizerName=PlayerStatePrioritizer, ClassName=/Script/IrisCore.NetObjectCountLimiter, ConfigClassName=/Script/Engine.PlayerStateCountLimiterConfig)
[/Script/IrisCore.NetObjectFilterDefinitions]
+NetObjectFilterDefinitions=(FilterName=Spatial, ClassName=/Script/IrisCore.NetObjectGridWorldLocFilter, ConfigClassName=/Script/IrisCore.NetObjectGridFilterConfig)
[/Script/IrisCore.ObjectReplicationBridgeConfig]
DefaultSpatialFilterName=Spatial
+FilterConfigs=(ClassName=/Script/Engine.Info, DynamicFilterName=None)
+PrioritizerConfigs=(ClassName=/Script/Engine.PlayerState, PrioritizerName=PlayerStatePrioritizer, bForceEnableOnAllInstances=true)
+DeltaCompressionConfigs=(ClassName=/Script/Engine.Pawn))
```

- **Which actors are spatially filtered by default:** `Source/Runtime/Engine/Private/Net/Iris/ReplicationSystem/EngineReplicationBridge.cpp:242–252`: every actor class whose CDO is not `bAlwaysRelevant`, `bOnlyRelevantToOwner` or `bNetUseOwnerRelevancy`. (A blog post said Iris spatially filters only `APawn` by default; that is **not** what 5.8.4's source does.)
- **Grid filter config** (`Public/Iris/ReplicationSystem/Filtering/NetObjectGridFilter.h:59–84`): `CellSizeX/Y = 20000`, `DefaultCullDistance = 15000`, `bUseExactCullDistance = true` (`:79–80`), `ViewPosRelevancyFrameCount = 2`, `DefaultFrameCountBeforeCulling = 4`.
- **Sphere defaults in source** (`Public/.../Prioritization/SphereNetObjectPrioritizer.h:19–35`): `InnerRadius = 2000`, `OuterRadius = 10000`, `InnerPriority = 1.0`, `OuterPriority = 0.2`, `OutsidePriority = 0.1`. Epic's 5.9 docs page lists InnerRadius 1000 / OuterRadius 5000; the **source** values are the ones in effect on 5.8.4.
- **Default prioritiser assignment** (`ObjectReplicationBridge.cpp:880–902`): a class config prioritiser if any, else a static priority if requested, else `DefaultSpatialNetObjectPrioritizerHandle` when the object has a world location.
- Hysteresis: `[/Script/IrisCore.ReplicationFilteringConfig] bEnableObjectScopeHysteresis=true, DefaultHysteresisFrameCount=6` (`BaseEngine.ini:1501–1503`).

## B7. The prioritiser API (what a project-level prioritiser can and cannot see)

`Public/Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h`:
- `:46–70` `FNetObjectPrioritizationParams`: `ObjectIndices`, `ObjectCount`, `float* Priorities`, `PrioritizationInfos`, `uint32 ConnectionId`, `FReplicationView View`. Comment (`:42–44`): "The prioritizer should honor the existing priority set for an object and only update the priority if the calculated value is higher than what is already stored. Prioritize() is only allowed to modify Priorities."
- `:86–89` `FNetObjectPrioritizationInfo { uint16 Data[4]; }` per object, zeroed.
- `:117–134` `FNetObjectPrioritizerAddObjectParams { OutInfo; InstanceProtocol; Protocol; StateBuffer; }`: **no `UObject*` and no `FNetRefHandle`**.
- `:155–160`: "Priorities should be at least 0.0f... At 1.0f objects are being considered for replication. Priorities are acumulated per object and connection until it's replicated, at which point the priority is reset to zero."
- `:162–210` `UCLASS(Abstract, MinimalAPI) class UNetObjectPrioritizer`: pure virtual `Init`, `Deinit`, `OnMaxInternalNetRefIndexIncreased`, `AddObject`, `RemoveObject`, `UpdateObjects`, `Prioritize`; exported `AddConnection`, `RemoveConnection`, `PrePrioritize`, `PostPrioritize`.

`Public/Iris/ReplicationSystem/ReplicationView.h:18–35`: `FView { FNetHandle Controller; FNetHandle ViewTarget; FVector Pos; FVector Dir; float FoVRadians; }`.

How the views are filled on the server (`Source/Runtime/Engine/Private/NetDriver.cpp:7683–7720`): `View.ViewTarget = FNetHandleManager::GetNetHandle(ViewTarget)` with `ViewTarget = Connection->GetConnectionViewTarget()` (`NetConnection.cpp:861–865`, the PC's view target, else the owning actor); then `ViewingController->GetPlayerViewPoint(View.Pos, ViewRotation)` overrides `Pos`, and `View.Controller = FNetHandleManager::GetNetHandle(ViewingController)`. `FNetHandleManager::GetNetHandle(const UObject*)` is `NETCORE_API static` (`Net/Core/Public/Net/Core/NetHandle/NetHandleManager.h:30`).

**Identity problem.** Nothing in the public API maps an `FInternalNetRefIndex` (what `AddObject`/`Prioritize` receive) to the object or its `FNetRefHandle`:
- `UNetObjectFilter::GetObjectIndex(FNetRefHandle)` exists but is **protected** and on the *filter* base (`NetObjectFilter.h:254, 270`).
- `FReplicationInstanceProtocol` is in a **Private** header (`Private/Iris/ReplicationSystem/ReplicationProtocol.h:41`).
- Custom rep tags for game properties exist only under `UE_NET_TEST_FAKE_REP_TAGS` ("Temp until proper RepTag support is added", `ReplicationStateDescriptorBuilder.cpp:2317–2333`).
- `USphereWithOwnerBoostNetObjectPrioritizer` gets owner data through `GetReplicationSystemInternal()->GetFiltering()` (`SphereWithOwnerBoostNetObjectPrioritizer.cpp:68`), a private path.

**The public path that works.** `UReplicationSystem::SetPrioritizer(FNetRefHandle, FNetObjectPrioritizerHandle)` (`ReplicationSystem.h:346`) is implemented as (`Private/Iris/ReplicationSystem/ReplicationSystem.cpp:1389–1407`):

```cpp
const FInternalNetRefIndex ObjectInternalIndex = ReplicationSystemInternal.GetNetRefHandleManager().GetInternalIndex(Handle);
...
return ReplicationSystemInternal.GetPrioritization().SetPrioritizer(ObjectInternalIndex, Prioritizer);
```

and `FReplicationPrioritization::SetPrioritizer` (`Prioritization/ReplicationPrioritization.cpp:414–471`) removes the object from its old prioritiser and calls the new one's `AddObject(ObjectIndex, AddParams)` **in the same call** (`:450–452`), returning its result. So a prioritiser can learn "the object being added right now is the one game code just passed" **[verified in source; not documented as a contract]**. Other public calls used: `GetPrioritizerHandle(FName)` (`ReplicationSystem.h:357`), `GetPrioritizer(FName)` (`:366`, "Can be useful for special initialization for a custom prioritizer"), `UObjectReplicationBridge::GetReplicatedRefHandle(const UObject*)` (`ObjectReplicationBridge.h:134`), `FReplicationSystemUtil::GetReplicationSystem(const AActor*)` / `GetActorReplicationBridge(const AActor*)` (`Engine/Public/Net/Iris/ReplicationSystem/ReplicationSystemUtil.h:80, 89`).

## B8. Subclassing the sphere prioritiser from another module

- `SphereNetObjectPrioritizer.h:38–47`: `UCLASS(Transient, MinimalAPI) class USphereNetObjectPrioritizer : public ULocationBasedNetObjectPrioritizer` with `IRISCORE_API virtual void Init / Deinit / Prioritize`. The batch helpers (`PrepareBatch`, `PrioritizeBatch`, `FinishBatch`, `:77–84`) are **not** exported, so a subclass in another module can call `Super::Prioritize` but not the helpers.
- `LocationBasedNetObjectPrioritizer.h:23–28`: `AddObject`, `RemoveObject`, `UpdateObjects`, `OnMaxInternalNetRefIndexIncreased` are `IRISCORE_API`; `GetLocation(const FObjectLocationInfo&)` (`:48`) too.
- The UHT output shipped with the engine exports the constructor, the vtable-helper constructor and the destructor for MinimalAPI classes: `Intermediate/Build/Win64/UnrealEditor/Inc/IrisCore/UHT/SphereNetObjectPrioritizer.generated.h:76–83` (`IRISCORE_API USphereNetObjectPrioritizer();`, `DECLARE_VTABLE_PTR_HELPER_CTOR(IRISCORE_API, ...)`, `IRISCORE_API virtual ~USphereNetObjectPrioritizer();`). So deriving across modules links in modular (editor) builds **[inferred; confirmed only when Phase 5 compiles it]**.
- `SphereNetObjectPrioritizer.cpp:12–18`: `Init` does `CastChecked<USphereNetObjectPrioritizerConfig>(Params.Config)`, so a config class must derive from `USphereNetObjectPrioritizerConfig`.
- `SphereNetObjectPrioritizer.cpp:301–316` `FinishBatch` writes `ExternalPriorities[ObjectIndex] = LocalPriorities[LocalObjIt]`; the local values start from the stored ones (`:132` loads `OriginalPriorities0123`) **[inferred: max of stored and computed]**.
- The owner-boost subclass is the shipped pattern for "distance + a bonus": `SphereWithOwnerBoostNetObjectPrioritizer.h:15–17` `OwnerPriorityBoost = 2.0f`, "Added to the priority calculated by the sphere prioritizer"; applied as `LocalPriorities[LocalIndex] += OwnerPriorityBoost` (`.cpp:167–176`).
- Registration (`Private/.../Prioritization/NetObjectPrioritizerDefinitions.h:9–39, 44–69`): `FNetObjectPrioritizerDefinition { PrioritizerName; ClassName; ConfigClassName; }` in `[/Script/IrisCore.NetObjectPrioritizerDefinitions]`; "The first valid definition will assume the role as default spatial prioritizer."

## B9. When prioritisation runs, and what happens to the priority afterwards

`ReplicationPrioritization.cpp:545–611` `Prioritize`: once per send, for each replicating connection that has a view (`:579`), objects "requiring priority update" are split per prioritiser (`PrioritizeForConnection`, `:733–805`) and each prioritiser's `Prioritize` is called with batches; then `ReplicationWriter->UpdatePriorities(Priorities)` (`:598`). After the prioritisers, the connection's own view target and controller are forced to a very high priority when `Net.RepGraph.ForceConnectionViewerPriority` is on (default **1**, shared with Replication Graph: `Net/Core/Private/Net/Core/Misc/NetCVars.cpp:9–13`; applied in `SetHighPriorityOnViewTargets`, `ReplicationPrioritization.cpp:801–836`).
