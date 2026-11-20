// P05 NetCostLab - workload tests: the seed-determined plan, checked without a world.
// Run: Tools\run_tests.bat NetCostLab.Workload (editor binary, headless).

#include "Misc/AutomationTest.h"
#include "NCLWorkloadSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NCLWorkloadTests
{
	/** Editor and game contexts; exactly one filter flag (ProductFilter), as the macro requires. */
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;

	using FSettings = UNCLWorkloadSubsystem::FSettings;

	FSettings MakeSettings(int32 Seed)
	{
		FSettings Settings;
		Settings.Seed = Seed;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLWorkloadPlanDeterministicTest, "NetCostLab.Workload.PlanIsDeterministic", NCLWorkloadTests::Flags)
bool FNCLWorkloadPlanDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace NCLWorkloadTests;
	const UNCLWorkloadSubsystem::FPlan First = UNCLWorkloadSubsystem::MakePlan(MakeSettings(1));
	const UNCLWorkloadSubsystem::FPlan Second = UNCLWorkloadSubsystem::MakePlan(MakeSettings(1));
	const UNCLWorkloadSubsystem::FPlan OtherSeed = UNCLWorkloadSubsystem::MakePlan(MakeSettings(2));

	TestEqual(TEXT("drone count"), First.DroneLoops.Num(), 256);
	TestEqual(TEXT("prop count"), First.PropPositions.Num(), 768);
	// uint32 has no TestEqual overload of its own (int32/int64/SIZE_T would be ambiguous), so compare explicitly.
	TestTrue(TEXT("same seed, same hash"), First.Hash == Second.Hash);
	TestTrue(TEXT("different seed, different hash"), First.Hash != OtherSeed.Hash);
	TestTrue(TEXT("same seed, bit-identical loops"), First.DroneLoops == Second.DroneLoops);   // FLoop's defaulted ==
	TestTrue(TEXT("same seed, identical prop positions"), First.PropPositions == Second.PropPositions);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLWorkloadPlanBoundsTest, "NetCostLab.Workload.PlanStaysInArena", NCLWorkloadTests::Flags)
bool FNCLWorkloadPlanBoundsTest::RunTest(const FString& Parameters)
{
	using namespace NCLWorkloadTests;
	const double Half = UNCLWorkloadSubsystem::ArenaHalfExtent;
	const UNCLWorkloadSubsystem::FPlan Plan = UNCLWorkloadSubsystem::MakePlan(MakeSettings(7));

	int32 LoopsOutside = 0;
	int32 SpeedsOutOfRange = 0;
	for (const UNCLWorkloadSubsystem::FLoop& Loop : Plan.DroneLoops)
	{
		LoopsOutside += (FMath::Abs(Loop.Center.X) + Loop.Radius > Half || FMath::Abs(Loop.Center.Y) + Loop.Radius > Half) ? 1 : 0;
		const double Speed = FMath::Abs(Loop.AngularSpeed) * Loop.Radius;
		SpeedsOutOfRange += (Speed < 400.0 - 1e-6 || Speed > 600.0 + 1e-6) ? 1 : 0;
	}
	int32 PropsOutside = 0;
	for (const FVector& Position : Plan.PropPositions)
	{
		PropsOutside += (FMath::Abs(Position.X) > Half || FMath::Abs(Position.Y) > Half) ? 1 : 0;
	}
	TestEqual(TEXT("drone loops that leave the arena"), LoopsOutside, 0);
	TestEqual(TEXT("drone speeds outside 4-6 m/s"), SpeedsOutOfRange, 0);
	TestEqual(TEXT("props outside the arena"), PropsOutside, 0);

	for (int32 Slot = 0; Slot < 32; ++Slot)
	{
		const UNCLWorkloadSubsystem::FLoop Avatar = UNCLWorkloadSubsystem::MakeAvatarLoop(7, Slot);
		const UNCLWorkloadSubsystem::FLoop Again = UNCLWorkloadSubsystem::MakeAvatarLoop(7, Slot);
		TestTrue(*FString::Printf(TEXT("avatar loop %d is deterministic"), Slot), Avatar == Again);
		TestTrue(*FString::Printf(TEXT("avatar loop %d stays in the arena"), Slot),
			FMath::Abs(Avatar.Center.X) + Avatar.Radius <= Half && FMath::Abs(Avatar.Center.Y) + Avatar.Radius <= Half);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLWorkloadLoopMotionTest, "NetCostLab.Workload.LoopMotionMatchesSpeed", NCLWorkloadTests::Flags)
bool FNCLWorkloadLoopMotionTest::RunTest(const FString& Parameters)
{
	UNCLWorkloadSubsystem::FLoop Loop;
	Loop.Center = FVector(100.0, -200.0, 300.0);
	Loop.Radius = 1000.0;
	Loop.AngularSpeed = 0.5;   // 0.5 rad/s at 10 m = 5 m/s
	Loop.Phase = 0.25;

	const FVector AtZero = UNCLWorkloadSubsystem::LoopPosition(Loop, 0.0);
	TestTrue(TEXT("t = 0 is at the phase angle"),
		AtZero.Equals(Loop.Center + FVector(1000.0 * FMath::Cos(0.25), 1000.0 * FMath::Sin(0.25), 0.0), 1e-6));
	TestEqual(TEXT("height stays at the centre's"), UNCLWorkloadSubsystem::LoopPosition(Loop, 3.7).Z, 300.0);

	// One server tick at 30 Hz: the chord is the arc to within 0.1 mm, so speed = distance / time.
	const double Tick = 1.0 / 30.0;
	const double Distance = FVector::Dist(UNCLWorkloadSubsystem::LoopPosition(Loop, 10.0), UNCLWorkloadSubsystem::LoopPosition(Loop, 10.0 + Tick));
	TestTrue(TEXT("5 m/s over one 30 Hz tick"), FMath::IsNearlyEqual(Distance / Tick, 500.0, 0.1));

	const double Period = UE_DOUBLE_TWO_PI / Loop.AngularSpeed;
	TestTrue(TEXT("a full period returns to the start"),
		UNCLWorkloadSubsystem::LoopPosition(Loop, Period).Equals(AtZero, 1e-6));

	// The facing is the direction of travel, in both directions of travel, and stays horizontal.
	for (const double AngularSpeed : {0.5, -0.5})
	{
		Loop.AngularSpeed = AngularSpeed;
		const FVector Step = UNCLWorkloadSubsystem::LoopPosition(Loop, 2.0 + 1e-4) - UNCLWorkloadSubsystem::LoopPosition(Loop, 2.0);
		const FRotator Facing = UNCLWorkloadSubsystem::LoopFacing(Loop, 2.0);
		TestTrue(*FString::Printf(TEXT("facing follows the motion (angular speed %.1f)"), AngularSpeed),
			FVector::DotProduct(Facing.Vector(), Step.GetSafeNormal()) > 0.9999);
		TestTrue(TEXT("facing has no pitch or roll"), FMath::IsNearlyZero(Facing.Pitch) && FMath::IsNearlyZero(Facing.Roll));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLWorkloadStreamTest, "NetCostLab.Workload.ChangeStreamsArePure", NCLWorkloadTests::Flags)
bool FNCLWorkloadStreamTest::RunTest(const FString& Parameters)
{
	using EStream = UNCLWorkloadSubsystem::EStream;
	const auto FirstDraws = [](int32 Seed, EStream Kind, int32 Index, int32 Number)
	{
		const FRandomStream Stream = UNCLWorkloadSubsystem::MakeStream(Seed, Kind, Index, Number);
		return TArray<float>{Stream.FRand(), Stream.FRand(), Stream.FRand()};
	};

	// The same key always gives the same values: a change does not depend on when, or after what, it is applied.
	TestTrue(TEXT("same key, same values"), FirstDraws(1, EStream::DroneState, 17, 5) == FirstDraws(1, EStream::DroneState, 17, 5));
	// Each part of the key matters.
	const TArray<float> Base = FirstDraws(1, EStream::DroneState, 17, 5);
	TestTrue(TEXT("another seed"), Base != FirstDraws(2, EStream::DroneState, 17, 5));
	TestTrue(TEXT("another kind"), Base != FirstDraws(1, EStream::DroneLoadout, 17, 5));
	TestTrue(TEXT("another object"), Base != FirstDraws(1, EStream::DroneState, 18, 5));
	TestTrue(TEXT("another change number"), Base != FirstDraws(1, EStream::DroneState, 17, 6));
	// Seeds far beyond what a signed product would hold are fine: the key is mixed without multiplication.
	TestTrue(TEXT("a huge seed is deterministic"), FirstDraws(MAX_int32, EStream::PropToggle, 767, 1000) == FirstDraws(MAX_int32, EStream::PropToggle, 767, 1000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLWorkloadScheduleTest, "NetCostLab.Workload.ScheduleKeepsPhase", NCLWorkloadTests::Flags)
bool FNCLWorkloadScheduleTest::RunTest(const FString& Parameters)
{
	UNCLWorkloadSubsystem::FSchedule Schedule{1.0, 0};
	TestEqual(TEXT("not due before its time"), UNCLWorkloadSubsystem::TakeDue(Schedule, 0.99, 2.0), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("first change is number 0"), UNCLWorkloadSubsystem::TakeDue(Schedule, 1.02, 2.0), 0);
	TestEqual(TEXT("next time is a period after the due time, not after now"), Schedule.NextTime, 3.0);
	TestEqual(TEXT("once per call: not due again in the same tick"), UNCLWorkloadSubsystem::TakeDue(Schedule, 1.02, 2.0), static_cast<int32>(INDEX_NONE));

	// A 5 s hitch: the change due at 3 s and the one due at 5 s are taken on two consecutive ticks, not both at once.
	TestEqual(TEXT("after a hitch, one change per tick (1)"), UNCLWorkloadSubsystem::TakeDue(Schedule, 6.0, 2.0), 1);
	TestEqual(TEXT("after a hitch, one change per tick (2)"), UNCLWorkloadSubsystem::TakeDue(Schedule, 6.033, 2.0), 2);
	TestEqual(TEXT("caught up: the next is at 7 s"), UNCLWorkloadSubsystem::TakeDue(Schedule, 6.066, 2.0), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("count of changes so far"), Schedule.Count, 3);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
