#pragma once

#include <chrono>
#include <immintrin.h>
#include <x86intrin.h> // Add this for __rdtsc and __rdtscp
#include <cpuid.h>
#include <iostream>

namespace hprof
{

double get_tsc_ghz()
{
    // 1. Hardware Method: CPUID Leaf 0x15 (Intel Skylake+)
    unsigned int eax, ebx, ecx, edx;
    __cpuid(0, eax, ebx, ecx, edx);
    
    if (eax >= 0x15)
    {
        __cpuid(0x15, eax, ebx, ecx, edx);
        // EBX/EAX is the ratio of TSC to the core crystal clock
        if (eax != 0 && ebx != 0) {
            // ECX is the crystal frequency. If 0, common default is 24MHz
            double crystal = (ecx != 0) ? static_cast<double>(ecx) : 24000000.0;

            const auto res{(crystal * ebx / eax) / 1e9};
            std::cout << "get_tsc_ghz __cpuid frequency: " << res << std::endl;
            return res;
        }
    }

    // 2. Fallback: Empirical Polling (No Sleep)
    using namespace std::chrono;
    
    // Warm up the CPU to ensure it's not in a deep sleep state
    {
        const auto endTP{steady_clock::now() + microseconds{500}};
        while(steady_clock::now() < endTP)
        {
            _mm_pause();
        }
    }

    const auto t1{steady_clock::now()};
    const uint64_t r1{__rdtsc()};

    // Active polling for exactly 20ms to get a statistically significant sample
    {
        const auto endTP{steady_clock::now() + milliseconds{20}};
        while(steady_clock::now() < endTP)
        {
            _mm_pause();
        }
    }

    const uint64_t r2{__rdtsc()};
    const auto t2{steady_clock::now()};

    // Calculate using ACTUAL time elapsed (robust against OS jitter)
    auto elapsed_ns{duration_cast<nanoseconds>(t2 - t1).count()};
    const auto res{static_cast<double>(r2 - r1) / elapsed_ns};
    std::cout << "get_tsc_ghz calibration frequency: " << res << std::endl;
    return res;
}

}