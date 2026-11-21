#pragma once

class MatchClock
{
public:
    constexpr void Reset() { *this = {}; }

    constexpr void Observe(unsigned frame, bool online, int gameSpeed)
    {
        // UI speed 6..0 is stored by the engine as 0..6.
        constexpr unsigned offlineRates[] = {60, 60, 30, 20, 15, 12, 10};
        constexpr unsigned onlineRates[] = {60, 45, 30, 20, 15, 12, 10};
        const unsigned index = gameSpeed >= 0 && gameSpeed <= 6 ? gameSpeed : 2u;
        const unsigned unitsPerFrame = UnitsPerSecond /
            (online ? onlineRates[index] : offlineRates[index]);

        // After loading a save, use its frame number and the restored speed.
        if (!m_unitsPerFrame || frame < m_lastFrame)
            m_elapsed = static_cast<unsigned long long>(frame) * unitsPerFrame;
        else
            m_elapsed += static_cast<unsigned long long>(frame - m_lastFrame) * m_unitsPerFrame;

        m_lastFrame = frame;
        m_unitsPerFrame = unitsPerFrame;
    }

    constexpr unsigned Seconds() const
    {
        return static_cast<unsigned>(m_elapsed / UnitsPerSecond);
    }

private:
    // A common multiple of the rates keeps partial seconds exact across speed changes.
    static constexpr unsigned UnitsPerSecond = 180;
    unsigned long long m_elapsed = 0;
    unsigned m_lastFrame = 0;
    unsigned m_unitsPerFrame = 0;
};
