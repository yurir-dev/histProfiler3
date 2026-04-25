#pragma once

#include <chrono>
#include "time_header.h"

namespace hprof{

    template<typename TimeUnits>
    const char* tuToStr() {
        if constexpr (std::is_same_v<TimeUnits, std::chrono::nanoseconds>) return "nanoseconds";
        else if constexpr (std::is_same_v<TimeUnits, std::chrono::microseconds>) return "microseconds";
        else if constexpr (std::is_same_v<TimeUnits, std::chrono::milliseconds>) return "milliseconds";
        else if constexpr (std::is_same_v<TimeUnits, std::chrono::seconds>) return "seconds";
        else if constexpr (std::is_same_v<TimeUnits, std::chrono::minutes>) return "minutes";
        else if constexpr (std::is_same_v<TimeUnits, std::chrono::hours>) return "hours";
        else return "units";
    }

    inline void printSmartDuration(std::ostream& os, std::chrono::steady_clock::duration d) {
        using namespace std::chrono;
        auto abs_d = (d < d.zero()) ? -d : d; // Handle negative if necessary

        if (abs_d >= hours(1))
            os << duration_cast<duration<double, std::ratio<3600>>>(d).count() << " hours";
        else if (abs_d >= minutes(1))
            os << duration_cast<duration<double, std::ratio<60>>>(d).count() << " minutes";
        else if (abs_d >= seconds(1))
            os << duration_cast<duration<double>>(d).count() << " seconds";
        else if (abs_d >= milliseconds(1))
            os << duration_cast<duration<double, std::milli>>(d).count() << " milliseconds";
        else if (abs_d >= microseconds(1))
            os << duration_cast<duration<double, std::micro>>(d).count() << " microseconds";
        else
            os << duration_cast<nanoseconds>(d).count() << " nanoseconds";
    }

    template <typename TimeUnits = void>
    class ScopedTimer
    {
        public:
        ScopedTimer(const char* desc = "NA", std::ostream& os = std::cout)
        : _desc{desc}, _os{os}, _startTP{std::chrono::steady_clock::now()}
        {}
        ~ScopedTimer()
        {
            const auto endTP{std::chrono::steady_clock::now()};
            const auto diff{endTP - _startTP};

            _os << TimeHeader{} << " : " << _desc << " : ";

            if constexpr (std::is_void_v<TimeUnits>)
            {
                // AUTO MODE: Pick units based on how long it took
                printSmartDuration(_os, diff);
            } 
            else
            {
                using FloatDuration = std::chrono::duration<double, typename TimeUnits::period>;
                const auto elapsed = std::chrono::duration_cast<FloatDuration>(diff);

                _os << TimeHeader{} << " : " << _desc << " : " 
                    << std::fixed << std::setprecision(3) << elapsed.count() 
                    << " " << tuToStr<TimeUnits>();
            }
            _os << std::endl;
            
        }

        private:
        const char* _desc{"NA"};
        std::ostream& _os;
        std::chrono::time_point<std::chrono::steady_clock> _startTP;
    };

}