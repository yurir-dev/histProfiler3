#pragma once

#include <iostream>
#include <array>
#include <string>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <filesystem>
#include <atomic>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace hprof
{
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


	template<size_t NumBucket, size_t SamplesPerBucket>
	struct Histogram final
	{
	public:
		Histogram() = default;
		Histogram(const std::string& label)
			:_samplesPerBucket{SamplesPerBucket}, _bucketsLen{NumBucket}
		{
			std::fill(_buckets.begin(), _buckets.end(), 0);

			_labelLen = std::min(_label.size(), label.size());
			std::strncpy(_label.data(), label.c_str(), _labelLen);

			_ready.store(true, std::memory_order_release);
		}

		void input(const uint64_t s)
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
				_overfows.fetch_add(1, std::memory_order_relaxed);;
			}
		}

		constexpr size_t getNumBuckets() const noexcept { return NumBucket; }
		constexpr uint64_t getHistMagicID() const noexcept { return 0x0101010101010101; }
		bool verifyMagic() const noexcept { return _id == getHistMagicID(); }

		bool isReady() const noexcept
		{
			return _ready.load(std::memory_order_relaxed);
		}

		const uint64_t _id{getHistMagicID()}; // used by shm readers, shm file could store different types 
		std::atomic<uint64_t> _maxSample{ 0 };
		std::atomic<uint64_t> _minSample{ 0xffffffffffffffffLL };
		std::atomic<uint64_t> _overfows{ 0 };
		std::atomic<uint64_t> _sum{ 0 };
		std::atomic<uint64_t> _numSamples{ 0 };
		std::atomic<uint64_t> _samplesPerBucket{0};
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
		uint64_t _bucketsLen;
		std::array<std::atomic<uint64_t>, NumBucket + 1> _buckets;
		std::atomic<bool> _ready{false};
	};

	template <typename HistType>
	std::ostream& dumpHistogram(std::ostream& os, const HistType& hist)
	{
		auto meanNS{safeDiv(hist._sum, hist._numSamples)};
		auto meanUnits{safeDiv(meanNS, hist._samplesPerBucket)};

		os << std::string_view{hist._label.data(), hist._labelLen}
		   << '\n'
		   << std::fixed << std::setprecision(3)
		   << "#buckets: " << hist.getNumBuckets()
		   << ", #samples: " << hist._numSamples
		   << ", #overflows: " << hist._overfows
		   << ", ns/bucket: " << hist._samplesPerBucket
		   << '\n'
		   << "mean: " << meanUnits << "(" << meanNS << " ns)"
		   << ", median: " << median(hist)
		   << ", min: " << hist._minSample << "ns, max: " << hist._maxSample << "ns"
		   << std::defaultfloat
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

	template<size_t WindowSize>
	class RateCounter final
	{
	public:
		RateCounter(const std::string& label)
		{
			_counterLen = WindowSize;
			std::fill(_numPerSecond.begin(), _numPerSecond.end(), 0);

			_labelLen = std::min(_label.size(), label.size());
			std::strncpy(_label.data(), label.c_str(), _labelLen);

			_ready.store(true, std::memory_order_release);
		}
		void sample(uint64_t val = 1)
		{
			const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
			const auto index{seconds % _numPerSecond.size()};
			
			
			if (_lastUpdatedSeconds != 0)
			{
				const auto diff{seconds - _lastUpdatedSeconds};
				if (diff > 1)
				{
					// gap detected
					if (diff >= WindowSize)
					{
						fillGap(0, _numPerSecond.size() - 1, 0);
					}
					else
					{
						const auto prevIndex{_lastUpdatedSeconds % _numPerSecond.size()};
						fillGap(prevIndex, index, 0);
					}
				}
			}

			_numPerSecond[index].fetch_add(val, std::memory_order_relaxed);
			_numPerSecond[(index + 1) % _numPerSecond.size()].store(0, std::memory_order_relaxed);

			_lastUpdatedSeconds = seconds;
		}

		constexpr uint64_t getRateCounterMagicID() const noexcept { return 0x0202020202020202; }
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
			return _ready.load(std::memory_order_relaxed);
		}

		const uint64_t _id{getRateCounterMagicID()}; // used by shm readers, shm file could store different types 
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
		uint64_t _counterLen{0};
		std::array<std::atomic<uint64_t>, WindowSize> _numPerSecond;
		uint64_t _lastUpdatedSeconds{0};
		std::atomic<bool> _ready{false};
	};

	template <typename RateCounterType>
	std::ostream& dumpRateCounter(std::ostream& os, const RateCounterType& rateCnt)
	{
		os << std::string_view{rateCnt._label.data(), rateCnt._labelLen} << "\n[";
		for (auto& val : rateCnt._numPerSecond)
		{
			os << val.load(std::memory_order_relaxed) << ',';
		}
		os << ']';
		return os;
	}

	enum class OpenFilePolicy {CreateNew, ReuseIfExists};

	template<typename ObjType>
	class ShmFile final
	{
	public:	
		ShmFile() = default;
		template <typename... Ts>
		ShmFile(std::filesystem::path filename, OpenFilePolicy opf, Ts... args)
		{
			struct RAII final
			{
				int _fd{ -1 };
				~RAII() { if (_fd != -1) { close(_fd); } }
			};
			RAII raii;

			bool isCreator = true;
			if (opf == OpenFilePolicy::CreateNew)
			{
				raii._fd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
			}
			else if (opf == OpenFilePolicy::ReuseIfExists)
			{
				raii._fd = ::open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
			}
			else
			{
				throw std::runtime_error{"Unvalid  OpenFilePolicy: " + std::to_string(static_cast<int>(opf))};
			}

			if (raii._fd == -1)
			{
				isCreator = false;
				if (errno == EEXIST)
				{
    				raii._fd = ::open(filename.c_str(), O_RDWR);
				}
			}
			if (raii._fd == -1)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to open " + std::string{filename} + ", errno: " + std::strerror(err)};
			}

			if (isCreator)
			{
				if (::ftruncate(raii._fd, sizeof(ObjType)) == -1)
				{
					const auto err{ errno };
					throw std::runtime_error{"FAILED to ftruncate " + std::string{filename} + ", errno: " + std::strerror(err)};
				}
			}

			void* beginAddr = mmap(NULL, sizeof(ObjType), PROT_READ | PROT_WRITE, MAP_SHARED, raii._fd, 0);
			if (beginAddr == MAP_FAILED)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to mmap " + std::string{filename} + ", errno: " + std::strerror(err)};
			}

			if (isCreator)
			{
				_objPtr = new (beginAddr) ObjType(args...);
			}
			else
			{
				_objPtr = reinterpret_cast<ObjType*>(beginAddr);
				if (!_objPtr->verifyMagic())
				{
					throw std::runtime_error{"FAILED to verify magic, corrupted file:  " + std::string{filename}};
				}
			}

    		std::cout << "Success to create shmFile: " << filename << ", addr: " << beginAddr << std::endl;
		}

		ShmFile(ShmFile&&) = default;
		ShmFile& operator=(ShmFile&&) = default;
		ShmFile(ShmFile&) = delete;
		ShmFile& operator=(ShmFile&) = delete;
		~ShmFile()
		{
			if (_objPtr != nullptr)
			{
    			if (-1 == msync(_objPtr, sizeof(ObjType), MS_SYNC))
				{
					const auto err{ errno };
					std::cerr << __FILE__ << ':' << __LINE__
						<< " FAILED to msync addr: " << static_cast<void*>(_objPtr) << ", errno: " << std::strerror(err) << std::endl;
				}
				if (-1 == munmap(_objPtr, sizeof(ObjType)))
				{
					const auto err{ errno };
					std::cerr << __FILE__ << ':' << __LINE__
						<< " FAILED to munmap addr: " << static_cast<void*>(_objPtr) << ", errno: " << std::strerror(err) << std::endl;
				}
			}
		}

		ObjType& get() {return *_objPtr;}

		private:
		ObjType* _objPtr{nullptr};
	};
} // namespace hprof