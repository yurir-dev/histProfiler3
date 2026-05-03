#pragma once

#include <ostream>      // For std::ostream
#include <iomanip>      // For std::setprecision
#include <ios>          // For std::fixed, std::defaultfloat
#include <string_view>  // For std::string_view
#include <array>        // For std::array
#include <string>       // For std::string
#include <cstring>      // For std::strncpy
#include <chrono>       // For std::chrono
#include <cmath>        // For std::round
#include <cerrno>       // For errno
#include <atomic>       // For std::atomic
#include <cstddef>      // For size_t (though often provided by others)
#include <cstdint>      // For uint64_t
#include <ctime>        // For clockid_t, timespec
#include <algorithm>    // For std::fill, std::min
#include <immintrin.h>  // For _mm_lfence, __rdtsc
#include <limits>		// For std::numeric_limits

#include "cpu_freq.h"

namespace hprof
{
	inline double safeDiv(uint64_t sum, uint64_t num) noexcept
	{
		return num > 0 ? static_cast<double>(sum) / static_cast<double>(num) : 0.0;
	}
	template <typename HistType>
	uint64_t median(const HistType& hist) noexcept
	{
		uint64_t halfSamples{ hist._numSamples.load(std::memory_order_relaxed) / 2 };
		uint64_t sum{ 0 };
		for (size_t i = 0; i < hist._buckets.size(); ++i)
		{
			sum += hist._buckets[i].load(std::memory_order_relaxed);
			if (sum >= halfSamples)
				return i;
		}
		return 0;
	}


	template<size_t NumBucket, size_t SamplesPerBucket>
	struct Histogram final
	{
	public:
		Histogram() = default;
		Histogram(const std::string& label)
			:_samplesPerBucket{SamplesPerBucket}, _bucketsLen{NumBucket}
		{
			//std::fill(_buckets.begin(), _buckets.end(), 0);

			_labelLen = std::min(_label.size() - 1, label.size());
			std::strncpy(_label.data(), label.c_str(), _labelLen);
			_label.data()[_labelLen] = '\0';

			_ready.store(true, std::memory_order_release);
		}

		void input(const uint64_t s) noexcept
		{
			if (s > _maxSample.load(std::memory_order_relaxed)) 
			{
				_maxSample.store(s, std::memory_order_relaxed);
			}
			if (s < _minSample.load(std::memory_order_relaxed))
			{
				_minSample.store(s, std::memory_order_relaxed);
			}

			_sum.fetch_add(s, std::memory_order_relaxed);
			_numSamples.fetch_add(1, std::memory_order_relaxed);

			size_t bucket = std::round(safeDiv(s, _samplesPerBucket.load(std::memory_order_relaxed)));
			if (bucket < _buckets.size())
			{
				_buckets[bucket].fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				_overflows.fetch_add(1, std::memory_order_relaxed);
			}
		}

		constexpr size_t getNumBuckets() const noexcept { return NumBucket; }
		bool verifyMagic() const noexcept { return _id == getHistMagicID(); }

		bool isReady() const noexcept
		{
			return _ready.load(std::memory_order_acquire);
		}

		static constexpr uint64_t getHistMagicID() noexcept { return 0x0101010101010101; }

		const uint64_t _id{getHistMagicID()}; // used by shm readers, shm file could store different types 
		std::atomic<uint64_t> _maxSample{ 0 };
		std::atomic<uint64_t> _minSample{ std::numeric_limits<uint64_t>::max() };
		std::atomic<uint64_t> _overflows{ 0 };
		std::atomic<uint64_t> _sum{ 0 };
		std::atomic<uint64_t> _numSamples{ 0 };
		std::atomic<uint64_t> _samplesPerBucket{0};
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
		uint64_t _bucketsLen{NumBucket};
		std::array<std::atomic<uint64_t>, NumBucket> _buckets;
		std::atomic<bool> _ready{false};
	};

	template <typename HistType>
	std::ostream& dumpHistogram(std::ostream& os, const HistType& hist, bool summaryOnly = false)
	{
		auto meanNS{safeDiv(hist._sum.load(std::memory_order_relaxed), hist._numSamples.load(std::memory_order_relaxed))};
		auto meanUnits{safeDiv(meanNS, hist._samplesPerBucket.load(std::memory_order_relaxed))};

		os << std::string_view{hist._label.data(), hist._labelLen}
		   << '\n'
		   << std::fixed << std::setprecision(3)
		   << "#buckets: " << hist.getNumBuckets()
		   << ", #samples: " << hist._numSamples.load(std::memory_order_relaxed)
		   << ", #overflows: " << hist._overflows.load(std::memory_order_relaxed)
		   << ", ns/bucket: " << hist._samplesPerBucket.load(std::memory_order_relaxed)
		   << '\n'
		   << "mean: " << meanUnits << "(" << meanNS << " ns)"
		   << ", median: " << median(hist)
		   << ", min: " << hist._minSample.load(std::memory_order_relaxed) 
		   << "ns, max: " << hist._maxSample.load(std::memory_order_relaxed) << "ns"
		   << std::defaultfloat
		   << '\n';

		if (!summaryOnly)
		{
			for (size_t i = 0; i < hist._buckets.size(); ++i)
				os << hist._buckets[i].load(std::memory_order_relaxed) << '\n';
		}

		return os;
	}

	template <typename HistType>
	class ScopedHistSampler final
	{
		public:
		[[gnu::always_inline]]
		explicit ScopedHistSampler(HistType& hist)
		: _startTP{std::chrono::steady_clock::now()}, _histRef{hist}
		{}

		[[gnu::always_inline]]
		~ScopedHistSampler()
		{
			const auto endTP = std::chrono::steady_clock::now();
			const auto diffNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(endTP - _startTP);
			_histRef.input(diffNanos.count());
		}

		ScopedHistSampler(ScopedHistSampler&) = delete;
		ScopedHistSampler& operator=(const ScopedHistSampler&) = delete;
		ScopedHistSampler(ScopedHistSampler&&) = delete;
		ScopedHistSampler& operator=(ScopedHistSampler&&) = delete;

		private:
		std::chrono::time_point<std::chrono::steady_clock> _startTP;
		HistType& _histRef;
	};

	/**
 	* Note: This sampler assumes an Invariant TSC.
 	* On modern x86 CPUs (Intel Skylake+ / AMD Zen+), the TSC ticks at a 
 	* constant frequency regardless of Turbo Boost or power-saving states.
 	* 
 	* If running on ancient hardware (pre-2010) or specific virtualized 
 	* environments without TSC pass-through, the nanosecond conversion 
 	* may drift if the CPU frequency scales.
 	*/
	template <typename HistType>
	class ScopedHistRdtscSampler final
	{
		public:
		[[gnu::always_inline]]
		explicit ScopedHistRdtscSampler(HistType& hist) noexcept 
		: _start{RdtscClock::getStart()}, _histRef{hist}
		{}

		[[gnu::always_inline]]
		~ScopedHistRdtscSampler() noexcept 
		{
			const auto end{RdtscClock::getEnd()};
			const auto nanos{RdtscClock::convertToNanos(end - _start)};
        	_histRef.input(nanos);
		}

		ScopedHistRdtscSampler(ScopedHistRdtscSampler&) = delete;
		ScopedHistRdtscSampler& operator=(const ScopedHistRdtscSampler&) = delete;
		ScopedHistRdtscSampler(ScopedHistRdtscSampler&&) = delete;
		ScopedHistRdtscSampler& operator=(ScopedHistRdtscSampler&&) = delete;

		private:
		uint64_t _start{0};
		HistType& _histRef;
		
	};

	/*
	ClockType:
	CLOCK_MONOTONIC - Most general profiling, Represents absolute elapsed time since some arbitrary point (usually boot).
	CLOCK_MONOTONIC_RAW - Similar to CLOCK_MONOTONIC, but it is not subject to NTP frequency adjustments.
	CLOCK_REALTIME - Displaying the current time/date to a user, Avoid for profiling
	CLOCK_PROCESS_CPUTIME_ID - Only ticks when the CPU is actually executing your process's instructions. 
							   If your thread is "sleeping" or waiting for I/O, this clock stops.
							   This is great for seeing CPU cost but terrible for measuring "latency" (which includes wait time).
	*/
	template <typename HistType, clockid_t ClockType = CLOCK_MONOTONIC_RAW>
	class ScopedHistClockSampler final
	{
		static constexpr long InvalidNanosVal{-1};

		public:
		[[gnu::always_inline]]
		explicit ScopedHistClockSampler(HistType& hist)
		: _startTP{0, InvalidNanosVal}, _histRef{hist}
		{
			if (clock_gettime(ClockType, &_startTP) != 0)
			{
				_startTP.tv_nsec = InvalidNanosVal;
			}
		}

		[[gnu::always_inline]]
		~ScopedHistClockSampler()
		{
			struct timespec endTP;
			if (_startTP.tv_nsec == InvalidNanosVal || (clock_gettime(ClockType, &endTP) != 0))
			{
				/*
					clock_gettime failed to fetch time, 
					insert whatever error handling is appropriate.
				*/
				return;
			}

			uint64_t diffNanos = endTP.tv_nsec < _startTP.tv_nsec ?
								(static_cast<uint64_t>(endTP.tv_sec - _startTP.tv_sec - 1) * 1'000'000'000ULL) + (1'000'000'000ULL + endTP.tv_nsec - _startTP.tv_nsec) 
								:
								(static_cast<uint64_t>(endTP.tv_sec - _startTP.tv_sec) * 1'000'000'000ULL) + (endTP.tv_nsec - _startTP.tv_nsec);

			_histRef.input(diffNanos);
		}

		ScopedHistClockSampler(ScopedHistClockSampler&) = delete;
		ScopedHistClockSampler& operator=(const ScopedHistClockSampler&) = delete;
		ScopedHistClockSampler(ScopedHistClockSampler&&) = delete;
		ScopedHistClockSampler& operator=(ScopedHistClockSampler&&) = delete;

		private:
		struct timespec _startTP;
		HistType& _histRef;
	};
	
} // namespace hprof