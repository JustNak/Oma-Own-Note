#pragma once

#include <QtGlobal>

class ViewZoom {
public:
    static constexpr int kMinPercent = 50;
    static constexpr int kMaxPercent = 300;
    static constexpr int kStepPercent = 10;
    static constexpr int kDefaultPercent = 100;

    ViewZoom() = default;

    static ViewZoom fromPercent(int raw);

    int percent() const { return m_percent; }

    qreal factor() const { return m_percent / 100.0; }

    ViewZoom stepped(int steps) const;

    bool operator==(const ViewZoom &other) const { return m_percent == other.m_percent; }
    bool operator!=(const ViewZoom &other) const { return m_percent != other.m_percent; }

private:
    explicit ViewZoom(int snappedPercent);
    int m_percent = kDefaultPercent;
};

inline ViewZoom ViewZoom::fromPercent(int raw)
{
    const int snapped = qRound(raw / double(kStepPercent)) * kStepPercent;
    return ViewZoom(qBound(kMinPercent, snapped, kMaxPercent));
}

inline ViewZoom ViewZoom::stepped(int steps) const
{
    return fromPercent(m_percent + steps * kStepPercent);
}

inline ViewZoom::ViewZoom(int snappedPercent)
    : m_percent(snappedPercent)
{
}
