#pragma once

#include <ctime>        // For std::time_t, std::tm, localtime_r
#include <chrono>       // For std::chrono::system_clock
#include <ostream>      // For std::ostream
#include <iomanip>      // For std::put_time, std::setfill, std::setw

namespace hprof{

    struct TimeHeader
    {
        explicit TimeHeader(const char* format = "%H:%M:%S")
        : _now{std::chrono::system_clock::now()}, _format{format}{}

        std::chrono::time_point<std::chrono::system_clock> _now;
        const char* _format{"%H:%M:%S"};
    };
    inline std::ostream& operator<<(std::ostream& os, const TimeHeader& th)
    {
        const std::time_t now_c{std::chrono::system_clock::to_time_t(th._now)};
        std::tm now_tm;
        localtime_r(&now_c, &now_tm);

        const auto duration{th._now.time_since_epoch()};
        const auto micros{std::chrono::duration_cast<std::chrono::microseconds>(duration).count() % 1000000};

        return os << std::put_time(&now_tm, th._format) 
                  << "." << std::setfill('0') << std::setw(6) << micros;
    }
}

