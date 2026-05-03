#pragma once

#include <functional>   // For std::function
#include <chrono>       // For std::chrono
#include <iomanip>      // For stream formatting (if used in TimeHeader)
#include <ostream>      // For std::ostream
#include <thread>       // For std::this_thread::yield
#include <cmath>        // For std::round
#include <algorithm>    // For std::min
#include <utility>      // For std::move
#include <string>       // For string manipulations
#include <cstddef>      // For size_t (though often provided by others)
#include <cstdint>      // for uint64_t and a like

#include "time_header.h"

namespace hprof{

    inline void defaultIdleFunc()
    {
        std::this_thread::yield();
    }

    template <typename Func>
    class RateTrigger
    {
        public:
        RateTrigger(Func func, double rate, size_t maxBurst = 1'000'000)
        : _func{std::move(func)}, _rate{rate}, _maxBurst{maxBurst}
        {}

        template <typename TimeUnits>
        void run(TimeUnits duration, std::ostream& os)
        {
            run(duration, os, [](){std::this_thread::yield();});
        }

        template <typename TimeUnits, typename IdleFunc>
        void run(TimeUnits duration, std::ostream& os, IdleFunc idleFunc = [](){std::this_thread::yield();})
        {
            auto nowTP{std::chrono::steady_clock::now()};
            const auto startTP{nowTP};
            const auto endTP{startTP + duration};

            size_t numTriggeredEvents{0};
            const double ratePerNs{_rate / 1'000'000'000.0};
  
            os << TimeHeader{} << " : start running" << std::endl;

            while (nowTP < endTP)
            {
                const auto nanosSinceStart{std::chrono::duration_cast<std::chrono::nanoseconds>(nowTP - startTP)};
                const auto shouldHave{static_cast<size_t>(std::round(ratePerNs * nanosSinceStart.count()))};
                const auto numEventsToTrigger{static_cast<int64_t>(shouldHave) - static_cast<int64_t>(numTriggeredEvents)};

                if (numEventsToTrigger > 0)
                {
                    const auto toRun{std::min(static_cast<size_t>(numEventsToTrigger), _maxBurst)};
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
        Func _func;
        double _rate{0};
        size_t _maxBurst{100'000};
    };


}

