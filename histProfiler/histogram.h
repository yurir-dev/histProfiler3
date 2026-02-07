#pragma once

#include <iostream>
#include <array>
#include <string>
#include <cstring>
#include <chrono>
#include <cmath>

namespace profiler
{
	template<size_t NumBucket>
	struct Histogram final
	{
	public:
		Histogram() = default;
		Histogram(const std::string& label, size_t samplesPerBucket)
			:_samplesPerBucket{samplesPerBucket}
		{
			std::fill(_buckets.begin(), _buckets.end(), 0);

			_labelLen = std::min(_label.size(), label.size());
			std::strncpy(_label.data(), label.c_str(), _labelLen);
		}

		void input(const uint64_t s)
		{
			if (s > _maxSample) { _maxSample = s; }
			if (s < _minSample) { _minSample = s; }

			_sum += s;
			++_numSamples;

			size_t bucket = static_cast<size_t>(std::round(static_cast<double>(s) / static_cast<double>(_samplesPerBucket)));
			if (bucket < _buckets.size())
				++_buckets[bucket];
			else
			{
				++_overfows;
			}
		}

		constexpr size_t getNumBuckets() const noexcept { return NumBucket; }

		std::array<uint64_t, NumBucket + 1> _buckets;
		uint64_t _maxSample{ 0 };
		uint64_t _minSample{ 0xfffffffffffffffLL };
		uint64_t _overfows{ 0 };
		uint64_t _sum{ 0 };
		uint64_t _numSamples{ 0 };
		uint64_t _samplesPerBucket{0};
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
	};

	inline double safeDiv(uint64_t sum, uint64_t num) noexcept
	{
		return num > 0 ? static_cast<double>(sum) / static_cast<double>(num) : 0.0;
	}
	template <typename HistType>
	uint64_t median(HistType& hist) noexcept
	{
		uint64_t halfSamples{ hist._numSamples / 2 };
		uint64_t sum{ 0 };
		for (size_t i = 0; i < hist._buckets.size(); ++i)
		{
			sum += hist._buckets[i];
			if (sum >= halfSamples)
				return i;
		}
		return 0;
	}

	template <typename HistType>
	std::ostream& dumpHistogram(std::ostream& os, const HistType& hist)
	{
		auto meanNS{safeDiv(hist._sum, hist._numSamples)};
		auto meanUnits{safeDiv(meanNS, hist._samplesPerBucket)};

		os << std::string_view{hist._label.data(), hist._labelLen}
		   << '\n'
		   << "#buckets: " << hist.getNumBuckets()
		   << ", #samples: " << hist._numSamples
		   << ", #overflows: " << hist._overfows
		   << ", ns/bucket: " << hist._samplesPerBucket
		   << '\n'
		   << "mean: " << meanUnits << "(" << meanNS << " ns)"
		   << ", median: " << median(hist)
		   << ", min: " << hist._minSample << "ns, max: " << hist._maxSample << "ns"
		   << '\n';

		for (size_t i = 0; i < hist._buckets.size(); ++i)
			os << hist._buckets[i] << '\n';

		return os;
	}

	template <typename HistType>
	class ScopedHistSampler final
	{
		public:
		ScopedHistSampler(HistType& hist)
		: _histRef{hist}, _startTP{std::chrono::steady_clock::now()}
		{}
		~ScopedHistSampler()
		{
			const auto endTP = std::chrono::steady_clock::now();
			const auto diffNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(endTP - _startTP);
			_histRef.input(diffNanos.count());
		}

		HistType& _histRef;
		std::chrono::time_point<std::chrono::steady_clock> _startTP;
	};
}