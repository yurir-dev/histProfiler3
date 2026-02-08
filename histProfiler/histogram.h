#pragma once

#include <iostream>
#include <array>
#include <string>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <filesystem>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace profiler
{
	template<size_t NumBucket, size_t SamplesPerBucket>
	struct Histogram final
	{
	public:
		Histogram() = default;
		Histogram(const std::string& label)
			:_samplesPerBucket{SamplesPerBucket}
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
		constexpr uint64_t getHistMagicID() const noexcept { return 0x0101010101010101; }

		uint64_t _histId{getHistMagicID()}; // used by shm readers, shm file could store different types 
		uint64_t _maxSample{ 0 };
		uint64_t _minSample{ 0xfffffffffffffffLL };
		uint64_t _overfows{ 0 };
		uint64_t _sum{ 0 };
		uint64_t _numSamples{ 0 };
		uint64_t _samplesPerBucket{0};
		uint64_t _labelLen{0};
		std::array<char, 256> _label;
		std::array<uint64_t, NumBucket + 1> _buckets;
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

	template<typename ObjType>
	class shmFile final
	{
	public:	
		shmFile() = default;

		template <typename... Ts>
		shmFile(std::filesystem::path filename, Ts... args)
		{
			struct RAII final
			{
				int _fd{ -1 };
				~RAII() { if (_fd != -1) { close(_fd); } }
			};
			RAII raii;
    		raii._fd = ::open(filename.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
			if (-1 == raii._fd)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to open " + std::string{filename} + ", errno: " + std::strerror(err)};
			}

			::ftruncate(raii._fd, sizeof(ObjType));
		
			void* beginAddr = mmap(NULL, sizeof(ObjType), PROT_READ | PROT_WRITE, MAP_SHARED, raii._fd, 0);
			if (beginAddr == MAP_FAILED)
			{
				const auto err{ errno };
				throw std::runtime_error{"FAILED to mmap " + std::string{filename} + ", errno: " + std::strerror(err)};
			}

			_objPtr = new (beginAddr) ObjType(args...);
		
    		std::cout << "Success to create shmFile: " << filename << ", addr: " << beginAddr << std::endl;
		}

		shmFile(shmFile&&) = default;
		shmFile& operator=(shmFile&&) = default;
		shmFile(shmFile&) = delete;
		shmFile& operator=(shmFile&) = delete;
		~shmFile()
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
}