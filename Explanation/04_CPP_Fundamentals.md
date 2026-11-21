# 04 · C++ Fundamentals: NetCostLab

**Rule:** a language concept is explained here the first time this project's code uses it, with a small example first. Current to **Phase 5, step S2**. Each later step adds sections as it adds code (`03`).

---

## 1. Macros: code that writes code before the compiler sees it

The preprocessor runs **before** the C++ compiler. A `#define` is text substitution: wherever the name appears, the preprocessor pastes the body.

```cpp
#define SQUARE(x) ((x) * (x))
int a = SQUARE(3);          // the compiler actually sees: int a = ((3) * (3));
```

Unreal uses macros for boilerplate that must be identical in every module. Our module file is one line:

```cpp
IMPLEMENT_MODULE(FDefaultModuleImpl, NetCostLabCore);
```

What it expands to depends on the build (`Core/Public/Modules/ModuleManager.h:943–960`, 5.8.4):
- **Modular builds** (the editor, one DLL per module): an `InitializeModule` function that the module manager calls right after it loads our DLL, and that returns a new `FDefaultModuleImpl`.
- **Monolithic builds** (the packaged game, one executable): a static `FStaticallyLinkedModuleRegistrant` object. Its constructor runs at program start and tells the module manager "a module called NetCostLabCore exists, and this is how to create it". A compile-time check (`UE_STATIC_ASSERT_WARN`) warns if the name does not match the module being compiled.

Writing either by hand in every module would be error-prone; the macro keeps it uniform, and the same source line works for both builds.

**Why macros are risky:** they ignore scopes and types. `SQUARE(i++)` increments `i` twice. Unreal's macros are written defensively, and we use them only where the engine requires them.

## 2. Conditional compilation: one define decides what code exists

```cpp
#if WITH_PUSH_MODEL
    #define MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName, Object) /* real work */
#else
    #define MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName, Object)   // nothing at all
#endif
```

This is the real shape of `Net/Core/Public/Net/Core/PushModel/PushModel.h:7` and `:480–489`. When `WITH_PUSH_MODEL` is 0, every `MARK_PROPERTY_DIRTY_FROM_NAME` in our game code compiles to **nothing**: there is no run-time switch that could turn it on later.

UnrealBuildTool sets `WITH_PUSH_MODEL` per **target** (`UEBuildTarget.cs:6573–6579`): 1 for the editor, 0 for games unless a target asks otherwise (`TargetRules.cs:1516–1527`). That one define is why configuration C2 can only run in the editor binary on a launcher engine (`01b` #2).

## 3. The One Definition Rule (ODR), and why you cannot flip a define against precompiled code

C++ requires every class, inline function and global to have **one** definition across the whole program. If two parts of a program are compiled with different defines, they can disagree about the same thing:

```cpp
// compiled with FEATURE=1                 // compiled with FEATURE=0
struct FCounter { int A; int B; };         struct FCounter { int A; };
```

Code from the left side writes `B` past the end of an object the right side allocated. Nothing warns you; the program just corrupts memory.

The launcher engine ships **precompiled** engine modules. Their defines are fixed. If our module were compiled with `WITH_PUSH_MODEL=1` while the precompiled `NetCore` was compiled with 0, the two would disagree on `PushModel.h`. That is why UnrealBuildTool refuses to change "unique build environment" settings on an installed engine (`RulesAssembly.cs:710–712`), and why we did not try the `bOverrideBuildEnvironment` escape hatch (`01b` #2).

## 4. Translation units and `#include`

Each `.cpp` file is compiled on its own, as one **translation unit**: the file plus everything it `#include`s. The compiler knows nothing about other `.cpp` files; the linker joins the results.

- `#include "Modules/ModuleManager.h"` pastes that header's text in. The path is relative to an include directory that UnrealBuildTool adds because our `Build.cs` depends on the `Core` module.
- **Include what you use (IWYU):** include the specific header that declares what you need, not a giant "everything" header. Compile times stay low and dependencies stay visible.

## 5. Reading the C# rules files

`*.Target.cs` and `*.Build.cs` are **C#**, compiled and run by UnrealBuildTool before any C++ is compiled. Two C# features appear in them:

```csharp
public class NetCostLabTarget : TargetRules                   // inheritance: a NetCostLabTarget is a TargetRules
{
    public NetCostLabTarget(TargetInfo Target) : base(Target)  // constructor chaining: run the base constructor first
    {
        Type = TargetType.Game;                               // set an inherited property
    }
}
```

And in the engine's own `TargetRules.cs:1522–1526`, a property with a computed default:

```csharp
public bool bWithPushModel
{
    get => bWithPushModelOverride ?? (Type == TargetType.Editor);   // "??": use the override if set, else the default
    set => bWithPushModelOverride = value;
}
```

`??` (null-coalescing) returns the left side unless it is `null`. `bWithPushModelOverride` is a `bool?` (a nullable bool), so "nobody set it" is a real third state, and the default can depend on the target type. Reading this one line is how we found that games get no push model.

## 6. Bit masks (in the run scripts)

The processor affinity of a process is a **bit mask**: bit *n* set means "may run on logical CPU *n*".

```python
p_cores = 0xFFFF            # bits 0–15 set: logical CPUs 0–15 (the 8 hyper-threaded P-cores here)
everything = (1 << 24) - 1  # 24 logical CPUs: bits 0–23 set = 0xFFFFFF
e_cores = everything & ~p_cores   # "all, AND NOT the P-cores" = 0xFF0000 (logical 16–23)
```

- `1 << n` shifts a 1 into bit *n*; `(1 << n) - 1` sets the lowest *n* bits.
- `~` flips every bit; `&` keeps only bits set in both.
- Hex is used because each hex digit is exactly four bits, so `0xFF0000` reads as "the top eight CPUs".

## 7. Calling the operating system from Python with `ctypes`

`Tools\netlab_env.py` calls two Win32 functions directly:

```python
kernel32 = ctypes.windll.kernel32
handle = kernel32.OpenProcess(0x0200 | 0x1000, False, pid)   # PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION
kernel32.SetProcessAffinityMask(handle, mask)
kernel32.CloseHandle(handle)
```

- `ctypes` loads a DLL and calls its C functions. Declaring `argtypes` and `restype` tells it the C types, so a 64-bit handle is not truncated to 32 bits (a classic bug on 64-bit Windows).
- Every `OpenProcess` must be matched by `CloseHandle`, in a `finally` block, or the handle leaks even when the call fails. This is the same resource rule as C++'s RAII, written by hand.

---

## Added in step S1 (the workload)

## 8. `constexpr` constants in a namespace

```cpp
namespace NCLWorkload
{
	constexpr double DroneChangePeriod = 2.0;     // a compile-time constant: no storage, no initialisation order
}
Schedule.NextTime += NCLWorkload::DroneChangePeriod;
```

`constexpr` means the value is known while compiling, so it costs nothing at run time and cannot be changed by accident. The namespace keeps the names out of the global scope: `NCLWorkload::DroneChangePeriod` cannot clash with another file's `DroneChangePeriod`. `using namespace NCLWorkload;` inside a function shortens the names there only. A namespace in a `.cpp` is private to that file. A constant that other files need goes in a header: `inline constexpr float CullDistance` in `namespace NCLNet` (`NCLNet.h`; `inline` makes one shared definition for every file that includes it), or a `static constexpr` class member such as `UNCLWorkloadSubsystem::ArenaHalfExtent`, which the tests read.

## 9. `if` with an initialiser (C++17)

```cpp
if (const AActor* Target = GetViewTarget(); Target && Target != this)
{
	// Target exists only inside this if/else
}
```

The part before `;` runs first; the condition after it decides. The variable's scope ends with the `if`, so it cannot be used by mistake later. Used in `ANCLPlayerController::GetPlayerViewPoint`.

## 10. Lambdas as predicates

```cpp
const int32 Slot = AvatarOwners.IndexOfByPredicate(
	[Exiting](const TWeakObjectPtr<AController>& Owner) { return Owner.Get() == Exiting; });
```

A lambda is an unnamed function written in place. `[Exiting]` **captures** the variable by value (a copy of the pointer), so the lambda can use it later; `[]` captures nothing. `IndexOfByPredicate` calls it for each element and returns the first index where it returns `true`, or `INDEX_NONE`.

## 11. Fixed-width integers and `static_cast`

`int8`, `uint8`, `int16`, `uint16`, `int32` are exactly 8, 16 or 32 bits on every platform. Smaller types matter for replicated data: the serializers send fewer bits. Converting between them is explicit:

```cpp
Drone->SetDroneIndex(static_cast<int16>(Index));   // Index is int32; we clamped it to MAX_int16 first
```

`static_cast` is a checked, visible conversion (the compiler rejects nonsense like casting an `FVector` to `int`). The value must fit: that is why `ReadSettings` clamps the drone count and `AddPlayer` stops at `MAX_int8` slots. A silent narrowing would wrap around and corrupt the index.

## 12. Nested structs and static member functions

```cpp
class UNCLWorkloadSubsystem : public UTickableWorldSubsystem
{
public:
	struct FLoop { FVector Center; double Radius = 0.0; /* … */ };
	static FVector LoopPosition(const FLoop& Loop, double Time);
};
```

A nested struct belongs to the class's name (`UNCLWorkloadSubsystem::FLoop`) because nothing else uses it. A `static` member function has no `this`: it can only use its parameters, which is exactly what makes `MakePlan` and `LoopPosition` testable without creating a subsystem or a world. Default member initialisers (`= 0.0`) mean a new `FLoop` is never left with garbage values.

## 13. Aggregate initialisation and `enum class` with an underlying type

```cpp
struct FSchedule { double NextTime = 0.0; int32 Count = 0; };
StateSchedules.Add({Now + 0.5, 0});            // braces fill the members in declaration order

enum class EStream : uint8 { DroneState, DroneLoadout, PropToggle, AvatarLoop };
Key = FCrc::TypeCrc32(static_cast<uint8>(EStream::PropToggle), Key);
```

A struct with only public data members and no constructors is an **aggregate**: `{a, b}` sets its members in order, and members left out keep their default initialisers. `enum class` names are scoped (`EStream::PropToggle`, never a bare `PropToggle`) and never convert to an integer by themselves, so an explicit `static_cast` is needed where the number is wanted. `: uint8` fixes the size, so the value hashed is always exactly one byte.

## 14. Range-based `for` over a braced list

```cpp
for (const double AngularSpeed : {0.5, -0.5}) { /* ... */ }
```

The braces make a `std::initializer_list<double>`, a tiny fixed array; the loop runs once per value. Used in the tests to check both directions of travel with the same code.

## 15. A defaulted `operator==` (C++20)

```cpp
struct FLoop
{
	FVector Center; double Radius = 0.0; double AngularSpeed = 0.0; double Phase = 0.0;
	bool operator==(const FLoop&) const = default;    // member by member, in declaration order
};
TestTrue(TEXT("same seed, bit-identical loops"), First.DroneLoops == Second.DroneLoops);
```

`= default` asks the compiler to write the comparison: equal when every member is equal (exact `==` on the doubles, which is what "bit-identical" means here). `TArray`'s own `==` then compares two arrays element by element, so the test needs no loop. UE 5.8 compiles as C++20, which allows this.

## 16. Shared pointers and futures: `MakeShared`, `TSharedRef`, `TSharedPtr`, `TSharedFuture`

```cpp
const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();   // one allocation, reference-counted, never null
TArray<TSharedPtr<FJsonValue>> Connections;                         // may be null; a TSharedRef converts to it
TSharedFuture<FString> CsvFile = FCsvProfiler::Get()->EndCapture(); // a value that arrives later
if (!CsvFile.IsValid() || CsvFile.IsReady()) { /* safe to quit */ }
```

Unreal's shared pointers count references: the object is deleted when the last `TSharedRef`/`TSharedPtr` to it goes away. `TSharedRef` can never be null, so it needs no check; `TSharedPtr` can. They are for plain C++ objects (like the JSON DOM), never for `UObject`s, which the garbage collector owns (`06` §8). A **future** is a handle to a result that another thread will produce: `IsReady()` asks without waiting, `Get()` would wait. The director polls `IsReady()` once per frame, so the server never blocks.
