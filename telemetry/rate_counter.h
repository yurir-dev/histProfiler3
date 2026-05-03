#pragma once

#include <string>           // For std::string
#include <string_view>      // For std::string_view in dumpRateCounter
#include <algorithm>        // For std::min, std::fill
#include <cstring>          // For std::strncpy
//#include <chrono>           // For std::chrono
#include <atomic>           // For std::atomic
#include <ostream>          // For std::ostream
#include <array>            // For std::array
#include <cstdint>          // For uint64_t (Essential for portability)

#include "cpu_freq.h"

namespace hprof
{
    template<size_t WindowSize>
	class RateCounter final
	{
	public:
		RateCounter(const std::string& label)
		{
			_counterLen = WindowSize;
			//std::fill(_numPerSecond.begin(), _numPerSecond.end(), 0);

			_labelLen = std::min(_label.size() - 1, label.size());
			std::strncpy(_label.data(), label.c_str(), _labelLen);
			_label.data()[_labelLen] = '\0';

			_ready.store(true, std::memory_order_release);
		}
		void sample(uint64_t val = 1)
		{
			//const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
			const auto seconds{RdtscClock::getNowNanos() / 1'000'000'000ULL};
			const auto index{seconds % _numPerSecond.size()};

			const auto lastUpdate{_lastUpdatedSeconds.load(std::memory_order_relaxed)};
			if (lastUpdate != 0)
			{
				const auto diff{seconds - lastUpdate};
				if (diff > 1)
				{
					// gap detected
					if (diff >= WindowSize)
					{
						fillGap(0, _numPerSecond.size() - 1, 0);
					}
					else
					{
						const auto prevIndex{lastUpdate % _numPerSecond.size()};
						const auto gapEnd{(index + _numPerSecond.size() - 1) % _numPerSecond.size()};
						fillGap(prevIndex, gapEnd, 0);
					}
				}
			}

			_numPerSecond[index].fetch_add(val, std::memory_order_relaxed);
			_numPerSecond[(index + 1) % _numPerSecond.size()].store(0, std::memory_order_relaxed);

			_lastUpdatedSeconds.store(seconds, std::memory_order_relaxed);
		}

		static constexpr uint64_t getRateCounterMagicID() noexcept { return 0x0202020202020202; }
		bool verifyMagic() const noexcept { return _id == getRateCounterMagicID(); }

		void fillGap(uint64_t fromInd, uint64_t toInd, uint64_t val)
		{
			if (fromInd > toInd)
			{
				for (size_t i = fromInd ; i < _numPerSecond.size(); ++i)
				{
					_numPerSecond[i].store(val, std::memory_order_relaxed);
				}
				fromInd = 0;
			}
			for (size_t i = fromInd ; i <= toInd; ++i)
			{
				_numPerSecond[i].store(val, std::memory_order_relaxed);
			}
		}

		bool isReady() const noexcept
		{
			return _ready.load(std::memory_order_acquire );
		}

		const uint64_t _id{getRateCounterMagicID()}; // used by shm readers, shm file could store different types 
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
		uint64_t _counterLen{0};
		std::array<std::atomic<uint64_t>, WindowSize> _numPerSecond;
		std::atomic<uint64_t> _lastUpdatedSeconds{0};
		std::atomic<bool> _ready{false};
	};

	template<size_t WindowSize>
	std::ostream& operator<<(std::ostream& os, const RateCounter<WindowSize>& rateCnt)
	{
		bool first = true;
		os << std::string_view{rateCnt._label.data(), rateCnt._labelLen} << "\n[";
		for (auto& val : rateCnt._numPerSecond)
		{
			if (!first)
			{
				os << ',';
			}
			else
			{
				first = false;
			}
			os << val.load(std::memory_order_relaxed);
		}
		os << ']';
		return os;
	}
} // namespace hprof