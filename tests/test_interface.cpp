

#include <random>
#include <iostream>
#include <thread>
#include <chrono>

#include "histogram.h"

void wasteTime(size_t cnt)
{
	for (size_t i = 0; i < cnt; ++i)
	{
		volatile double s = std::sqrt(i + 1024 * 1024);
		s = s * s;
		s = std::sqrt(s);
	}
}

int testMacros()
{
	histProfiler::Histogram<200, 1000> hist{"basic test of micros"};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 1000, 100 };

	for (size_t i = 0; i < 1024 * 1024; i++)
	{
		const auto timeToWaist{static_cast<size_t>(std::round(dist(gen)))};
		{
			histProfiler::ScopedHistSampler shs{hist};
			wasteTime(timeToWaist);
		}
	}

	dumpHistogram(std::cout, hist);

	return 0;
}
int testMillis()
{
	histProfiler::Histogram<100, 1'000'000> hist{"basic test of millis"};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 50, 10 };

	for (size_t i = 0; i < 1024 ; i++)
	{
		const auto timeToWaist{static_cast<size_t>(std::round(dist(gen)))};
		{
			histProfiler::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	dumpHistogram(std::cout, hist);

	return 0;
}

int testShmHist()
{
	histProfiler::ShmFile<histProfiler::Histogram<100, 1'000'000>> shmCont{"basicTestMillisInShm", "basic test of millis"};
	auto& hist{shmCont.get()};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 50, 10 };

	for (size_t i = 0; i < 1024 ; i++)
	{
		const auto timeToWaist{static_cast<size_t>(std::round(dist(gen)))};
		{
			histProfiler::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	dumpHistogram(std::cout, hist);

	return 0;
}


int main(int /*argc*/, char* /*argv*/ [])
{
	if (auto res = testMacros() ; res != 0)
	{
		return res;
	}
	if (auto res = testMillis() ; res != 0)
	{
		return res;
	}
	if (auto res = testShmHist() ; res != 0)
	{
		return res;
	}
	return 0;
}