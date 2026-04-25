#pragma once

#include <functional>
#include <chrono>
#include <iomanip>
#include <ostream>
#include <thread>
#include <cmath>
#include <algorithm> // for std::min
#include <ctime> // for localtime_r

#include "time_header.h"

namespace hprof{

    class RateTrigger
    {
        public:
        RateTrigger(std::function<bool(void)> func, double rate, size_t maxBurst = 1'000'000)
        : _func{std::move(func)}, _rate{rate}, _maxBurst{maxBurst}
        {}

        template <typename TimeUnits>
        void run(TimeUnits duration,
                 std::ostream& os, 
                 const std::function<void()>& idleFunc= [](){ std::this_thread::yield(); })
        {
            auto nowTP{std::chrono::steady_clock::now()};
            const auto startTP{nowTP};
            const auto endTP{startTP + duration};

            size_t numTriggeredEvents{0};
            const double rateNS{_rate / 1'000'000'000.0};
  
            os << TimeHeader{} << " : start running" << std::endl;

            while (nowTP < endTP)
            {
                const auto nanosSinceStart{std::chrono::duration_cast<std::chrono::nanoseconds>(nowTP - startTP)};
                const auto shouldHave{static_cast<size_t>(std::round(rateNS * nanosSinceStart.count()))};
                const auto numEventsToTrigger{shouldHave - numTriggeredEvents};

                if (numEventsToTrigger > 0)
                {
                    const auto toRun{std::min(numEventsToTrigger, _maxBurst)};
                    for(size_t i = 0 ; i < toRun ; ++i)
                    {
                        if (!_func())
                        {
                            os << TimeHeader{} 
                               << " : user func returned false, finishing, triggered: " 
                               << numTriggeredEvents << std::endl;
                            return;
                        }
                        numTriggeredEvents += 1;
                    }
                }
                else
                {
                    idleFunc();
                }

                nowTP = std::chrono::steady_clock::now();
            }

            os << TimeHeader{} << " : finished executing, triggered: " << numTriggeredEvents << std::endl;
        }

        private:
        std::function<bool(void)> _func{[](){return false;}};
        double _rate{0};
        size_t _maxBurst;
    };


}

