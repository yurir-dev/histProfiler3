

#include <random>
#include <iostream>
#include <thread>
#include <chrono>


#include "latency_stats.h"
#include "rate_trigger.h"
#include "scoped_timer.h"

void move_to_register(const volatile void* ptr) {
	// Tells the compiler: "I'm using this memory, don't optimize the math that created it."
    asm volatile("" : : "g"(ptr) : "memory");
}

void wasteTime(size_t cnt)
{
	for (size_t i = 0; i < cnt; ++i)
	{
		volatile double s = std::sqrt(i + 1024 * 1024);
		s = s * s;
		s = std::sqrt(s);
		move_to_register(&s);
	}
}

template <template <typename> typename SamplerType>
int testMicros()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer st{__FUNCTION__};

	using HistMicros = hprof::Histogram<200, 1000>; 

	HistMicros hist{"basic test of micros"};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 50, 10 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{15}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		{
			SamplerType<HistMicros> shs{hist};
			wasteTime(timeToWaist);
		}
	}

	dumpHistogram(std::cout, hist);

	return 0;
}

template <template <typename> typename SamplerType>
int testMillis()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer st{__FUNCTION__};

	hprof::Histogram<100, 1'000'000> hist{"basic test of millis"};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 50, 10 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{15}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		{
			hprof::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	hprof::dumpHistogram(std::cout, hist);

	return 0;
}

int testShmHist()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer st{__FUNCTION__};

	hprof::ShmFile<hprof::Histogram<100, 1'000'000>> shmCont{"basicTestMillisInShm", 
															hprof::OpenFilePolicy::CreateNew,
															"basic test of millis"};
	auto& hist{shmCont.get()};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 50, 2 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{15}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		{
			hprof::ScopedHistSampler shs{hist};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	hprof::dumpHistogram(std::cout, hist);

	return 0;
}

int testRateCounter()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer st{__FUNCTION__};

	hprof::RateCounter<16> rateCnt{"basic test of rate counter"};

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

	hprof::dumpRateCounter(std::cout, rateCnt);

	return 0;
}

int testShmRateCounter()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer st{__FUNCTION__};

	hprof::ShmFile<hprof::RateCounter<128>> shmCont{"basicTestRateCounter", 
													hprof::OpenFilePolicy::ReuseIfExists,
													"basic test of rate counter"};
	auto& rateCnt{shmCont.get()};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 20, 2 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{10}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		rateCnt.sample();

		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
		std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
	}

	hprof::dumpRateCounter(std::cout, rateCnt);

	return 0;
}

int testShmRateCounterWithGaps()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer<std::chrono::seconds> st{"testShmRateCounterWithGaps"};

	hprof::ShmFile<hprof::RateCounter<128>> shmCont{"basicTestRateCounterGaps", 
													hprof::OpenFilePolicy::ReuseIfExists,
													"gap detection test of rate counter"};
	auto& rateCnt{shmCont.get()};

	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 20, 2 };

	const auto endTP{std::chrono::steady_clock::now() + std::chrono::seconds{120}};
	while(std::chrono::steady_clock::now() < endTP)
	{
		rateCnt.sample();

		const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
		if (seconds % 30 == 0)
		{
			std::this_thread::sleep_for(std::chrono::seconds{10});
		}
		else
		{
			const auto randomVal{std::round(dist(gen))};
			const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};
			std::this_thread::sleep_for(std::chrono::milliseconds{timeToWaist});
		}
	}

	hprof::dumpRateCounter(std::cout, rateCnt);

	return 0;
}

int testRateTrigger()
{
	std::cout << '\n' << hprof::TimeHeader{} << " : start " << __FUNCTION__ << std::endl;
	hprof::ScopedTimer<std::chrono::seconds> st{__FUNCTION__};

	size_t cnt{0};
	hprof::RateTrigger rt{
		[&cnt]() -> bool {
		cnt++;
		std::cout << hprof::TimeHeader{} << " : cnt: " << cnt << std::endl;
		return true;
	}, 1.0, 1};

	rt.run(std::chrono::seconds{10}, std::cout);
	rt.run(std::chrono::seconds{10}, std::cout, [](){std::this_thread::yield();});
	rt.run(std::chrono::seconds{10}, std::cout, [](){std::this_thread::sleep_for(std::chrono::milliseconds{10});});

	hprof::RateTrigger rtWorkOnce{
		[&cnt]() -> bool {
		cnt++;
		std::cout << hprof::TimeHeader{} << " : rtWorkOnce cnt: " << cnt << std::endl;
		return false;
	}, 1.0, 1};

	rtWorkOnce.run(std::chrono::seconds{10}, std::cout);

	if(30 <= cnt && cnt <= 31)
	{
		return 0;
	}
	return __LINE__;
}

template <template <typename> typename SamplerType>
int testSamplerOverhead()
{
	hprof::Histogram<1000, 1> hist{"testSamplerOverhead"};
	for(int i = 0; i < 1000000; ++i)
	{
    	SamplerType sampler(hist);
    	// Do absolutely nothing
	}
	hprof::dumpHistogram(std::cout, hist, true /*summaryOnly*/);

	return 0;
}

int testProcessCpuTime()
{
	std::random_device rd{};
	std::mt19937 gen{ rd() };
	std::normal_distribution<> dist{ 100, 10 };

	hprof::Histogram<100, 1> hist{"testProcessCpuTime , micros"};
	for(int i = 0; i < 1000; ++i)
	{
		const auto randomVal{std::round(dist(gen))};
		const auto timeToWaist{randomVal > 0 ? static_cast<size_t>(randomVal) : 0};

		{
    		hprof::ScopedHistClockSampler<decltype(hist), CLOCK_PROCESS_CPUTIME_ID> sampler(hist);    	
			wasteTime(timeToWaist); // should count only this
			std::this_thread::sleep_for(std::chrono::milliseconds{1}); // should not count this
		}
	}

	hprof::dumpHistogram(std::cout, hist, false /*summaryOnly*/);

	return 0;
}

int main(int /*argc*/, char* /*argv*/ [])
{
	if (auto res = testSamplerOverhead<hprof::ScopedHistSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testSamplerOverhead<hprof::ScopedHistRTDCSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testSamplerOverhead<hprof::ScopedHistClockSampler>() ; res != 0)
	{
		return res;
	}

	if (auto res = testProcessCpuTime() ; res != 0)
	{
		return res;
	}

	if (auto res = testMicros<hprof::ScopedHistSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testMicros<hprof::ScopedHistRTDCSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testMicros<hprof::ScopedHistClockSampler>() ; res != 0)
	{
		return res;
	}
	
	if (auto res = testMillis<hprof::ScopedHistSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testMillis<hprof::ScopedHistRTDCSampler>() ; res != 0)
	{
		return res;
	}
	if (auto res = testMillis<hprof::ScopedHistClockSampler>() ; res != 0)
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
	if (auto res = testShmRateCounterWithGaps() ; res != 0)
	
		return res;
	
	if (auto res = testRateTrigger() ; res != 0)
	{
		return res;
	}
	return 0;
}