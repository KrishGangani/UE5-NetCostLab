// P05 NetCostLab - a fixed-resolution histogram for the clients' correctness measurements.

#pragma once

#include "CoreMinimal.h"

/**
 * Counts non-negative values in bins one unit wide (1 cm, 1 ms), plus one overflow bin. Counts are exact, so the
 * histograms of many clients and runs can be added together before percentiles are taken (divergence.py does).
 * Percentile(P) is the nearest-rank percentile at bin resolution: the lower edge of the bin in which the cumulative
 * count first reaches P% of all values. Negative values (a position shown slightly ahead of the truth, from
 * quantisation) are counted in bin 0 and in NegativeCount.
 */
struct FNCLHistogram
{
	explicit FNCLHistogram(int32 InNumBins = 1)
	{
		Bins.SetNumZeroed(FMath::Max(InNumBins, 1) + 1);
	}

	void Add(double Value)
	{
		NegativeCount += Value < 0.0 ? 1 : 0;
		const int32 Bin = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(Value), 0.0, static_cast<double>(Bins.Num() - 1)));
		++Bins[Bin];
		++Count;
		Sum += Value;
		Max = Count == 1 ? Value : FMath::Max(Max, Value);
	}

	/** P in [0, 100]. Returns 0 for an empty histogram; the overflow bin reports its lower edge (NumBins). */
	double Percentile(double P) const
	{
		if (Count == 0)
		{
			return 0.0;
		}
		const int64 Rank = FMath::Max<int64>(1, static_cast<int64>(FMath::CeilToDouble(P / 100.0 * static_cast<double>(Count))));
		int64 Cumulative = 0;
		for (int32 Bin = 0; Bin < Bins.Num(); ++Bin)
		{
			Cumulative += Bins[Bin];
			if (Cumulative >= Rank)
			{
				return Bin;
			}
		}
		return Bins.Num() - 1;
	}

	double Mean() const { return Count > 0 ? Sum / static_cast<double>(Count) : 0.0; }

	TArray<int32> Bins;
	int64 Count = 0;
	int64 NegativeCount = 0;
	double Sum = 0.0;
	double Max = 0.0;
};
