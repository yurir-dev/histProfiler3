

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
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
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
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		{
			histProfiler::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	histProfiler::dumpHistogram(std::cout, hist);

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
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		{
			histProfiler::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	histProfiler::dumpHistogram(std::cout, hist);

	return 0;
}

int testRateCounter()
{
	histProfiler::RateCounter<16> rateCnt{"basic test of rate counter"};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 20, 2 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{30}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		rateCnt.sample();

		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
	}

	histProfiler::dumpRateCounter(std::cout, rateCnt);

	return 0;
}

int testShmRateCounter()
{
	histProfiler::ShmFile<histProfiler::RateCounter<16>> shmCont{"basicTestRateCounter", "basic test of rate counter"};
	auto& rateCnt{shmCont.get()};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 20, 2 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{30}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		rateCnt.sample();

		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
	}

	histProfiler::dumpRateCounter(std::cout, rateCnt);

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
	if (auto res = testRateCounter() ; res != 0)
	{
		return res;
	}
	if (auto res = testShmRateCounter() ; res != 0)
	{
		return res;
	}
	return 0;
}