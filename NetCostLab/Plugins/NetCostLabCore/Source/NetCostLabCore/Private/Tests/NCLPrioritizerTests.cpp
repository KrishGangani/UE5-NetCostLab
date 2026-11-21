// P05 NetCostLab - prioritiser tests: the focus rule (ApplyFocusBoost) on hand-made arrays, without Iris running.
// Run: Tools\run_tests.bat NetCostLab.Prioritizer (editor binary, headless).

#include "Misc/AutomationTest.h"
#include "NCLGameplayPrioritizer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NCLPrioritizerTests
{
	/** Editor and game contexts; exactly one filter flag (ProductFilter), as the macro requires. */
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
	constexpr float Boost = 2.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLFocusBoostTest, "NetCostLab.Prioritizer.FocusBoost", NCLPrioritizerTests::Flags)
bool FNCLFocusBoostTest::RunTest(const FString& Parameters)
{
	using namespace NCLPrioritizerTests;

	// Objects 0..5 with sphere priorities; focus ids: 1 -> avatar 100, 3 -> avatar 200, 5 -> avatar 100, others none.
	const TArray<uint64> Focus = {0, 100, 0, 200, 0, 100};
	const TArray<uint32> Batch = {1, 2, 3, 5};          // object 0 and 4 are not in this send's batch
	TArray<float> Priorities = {0.5f, 0.1f, 0.2f, 0.3f, 0.4f, 1.0f};

	// One view whose target is avatar 100: objects 1 and 5 are boosted, 3 (another player's threat) and 2 are not.
	TArray<FNCLFocusView> Player100 = {{100, 7}};
	const int32 Boosted = UNCLGameplayPrioritizer::ApplyFocusBoost(Batch, Priorities, Focus, Player100, Boost);
	TestEqual(TEXT("two objects focus on this view's avatar"), Boosted, 2);
	TestEqual(TEXT("object 1 boosted"), Priorities[1], 0.1f + Boost);
	TestEqual(TEXT("object 5 boosted"), Priorities[5], 1.0f + Boost);
	TestEqual(TEXT("object 3 (another player's) untouched"), Priorities[3], 0.3f);
	TestEqual(TEXT("object 2 (no focus) untouched"), Priorities[2], 0.2f);
	TestEqual(TEXT("object 0 (not in the batch) untouched"), Priorities[0], 0.5f);

	// The controller id matches as well as the view target id.
	TArray<float> ByController = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	TArray<FNCLFocusView> ControllerView = {{999, 200}};
	TestEqual(TEXT("a match on the controller counts"),
		UNCLGameplayPrioritizer::ApplyFocusBoost(Batch, ByController, Focus, ControllerView, Boost), 1);
	TestEqual(TEXT("object 3 boosted through the controller"), ByController[3], Boost);

	// Several views (split screen): an object matching two of them is boosted once.
	TArray<float> Multi = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	TArray<FNCLFocusView> TwoViews = {{100, 1}, {200, 100}};
	TestEqual(TEXT("multiple views: objects 1, 3 and 5"), UNCLGameplayPrioritizer::ApplyFocusBoost(Batch, Multi, Focus, TwoViews, Boost), 3);
	TestEqual(TEXT("object 1 boosted once although two views match"), Multi[1], Boost);

	// No focus (id 0) never matches, not even a view with id 0 (an invalid handle).
	TArray<float> InvalidView = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	TArray<FNCLFocusView> ZeroIds = {{0, 0}};
	TestEqual(TEXT("an invalid focus never matches an invalid view"),
		UNCLGameplayPrioritizer::ApplyFocusBoost(Batch, InvalidView, Focus, ZeroIds, Boost), 0);

	// Zero objects, and an index beyond the focus table (an object added after the table grew) are harmless.
	TArray<float> Untouched = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
	TestEqual(TEXT("zero objects"), UNCLGameplayPrioritizer::ApplyFocusBoost({}, Untouched, Focus, Player100, Boost), 0);
	const TArray<uint32> Beyond = {7};
	TestEqual(TEXT("an index beyond the focus table is not boosted"),
		UNCLGameplayPrioritizer::ApplyFocusBoost(Beyond, Untouched, Focus, Player100, Boost), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
