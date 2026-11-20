// Clock: where a Screen gets the time from.
//
// The integration layer implements it over the game's UI clock. ManualClock is
// for tests and headless tools: time moves only when told to.
#pragma once

namespace nativeui {

    class Clock {
    public:
        virtual ~Clock() = default;
        // Seconds since any fixed moment; only differences are used. Monotonic.
        virtual double NowSeconds() const = 0;
    };

    class ManualClock final : public Clock {
    public:
        double NowSeconds() const override { return now_; }
        void Advance(double seconds) { now_ += seconds; }

    private:
        double now_ = 0.0;
    };

}
