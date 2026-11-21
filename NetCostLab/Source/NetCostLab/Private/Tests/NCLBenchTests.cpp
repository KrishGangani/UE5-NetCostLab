// P05 NetCostLab - benchmark measurement tests: the histogram the clients' error and age statistics come from.
// Run: Tools\run_tests.bat NetCostLab.Bench (editor binary, headless).

#include "Misc/AutomationTest.h"
#include "NCLHistogram.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NCLBenchTests
{
	/** Editor and game contexts; exactly one filter flag (ProductFilter), as the macro requires. */
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNCLHistogramTest, "NetCostLab.Bench.HistogramPercentiles", NCLBenchTests::Flags)
bool FNCLHistogramTest::RunTest(const FString& Parameters)
{
	FNCLHistogram Empty(10);
	TestEqual(TEXT("empty: percentile 0"), Empty.Percentile(50.0), 0.0);
	TestEqual(TEXT("empty: mean 0"), Empty.Mean(), 0.0);

	// 1..100: nearest-rank percentiles at 1-unit bins are exact here.
	FNCLHistogram Hundred(1000);
	for (int32 Value = 1; Value <= 100; ++Value)
	{
		Hundred.Add(Value + 0.25);         // inside bin Value
	}
	TestEqual(TEXT("p50 of 1..100"), Hundred.Percentile(50.0), 50.0);
	TestEqual(TEXT("p95 of 1..100"), Hundred.Percentile(95.0), 95.0);
	TestEqual(TEXT("p99 of 1..100"), Hundred.Percentile(99.0), 99.0);
	TestEqual(TEXT("p100 is the largest bin"), Hundred.Percentile(100.0), 100.0);
	TestEqual(TEXT("count"), static_cast<int32>(Hundred.Count), 100);
	TestTrue(TEXT("mean of the values added"), FMath::IsNearlyEqual(Hundred.Mean(), 50.75, 1e-9));
	TestTrue(TEXT("max of the values added"), FMath::IsNearlyEqual(Hundred.Max, 100.25, 1e-9));

	// Out of range: negatives in bin 0 (and counted), large values in the overflow bin.
	FNCLHistogram Edges(10);
	Edges.Add(-3.0);
	Edges.Add(1e9);
	TestEqual(TEXT("negatives counted"), static_cast<int32>(Edges.NegativeCount), 1);
	TestEqual(TEXT("a negative value falls in bin 0"), Edges.Percentile(50.0), 0.0);
	TestEqual(TEXT("overflow reports the overflow bin"), Edges.Percentile(100.0), 10.0);
	TestTrue(TEXT("max keeps the real value"), Edges.Max == 1e9);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
