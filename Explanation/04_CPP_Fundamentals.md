# 04 · C++ Fundamentals: NetCostLab

**Rule:** a language concept is explained here the first time this project's code uses it, with a small example first. Current to **Phase 4** (the foundation). Phase 5 adds sections as each step adds code (`03`).

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
