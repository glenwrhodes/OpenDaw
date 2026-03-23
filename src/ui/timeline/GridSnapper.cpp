#include "GridSnapper.h"
#include <algorithm>
#include <cmath>

namespace OpenDaw {

double GridSnapper::gridIntervalBeats() const
{
    switch (mode_) {
    case SnapMode::Off:               return 0.0;
    case SnapMode::EighthBeat:        return 0.125;
    case SnapMode::TripletSixteenth:  return 1.0 / 6.0;
    case SnapMode::QuarterBeat:       return 0.25;
    case SnapMode::TripletEighth:     return 1.0 / 3.0;
    case SnapMode::HalfBeat:          return 0.5;
    case SnapMode::TripletQuarter:    return 2.0 / 3.0;
    case SnapMode::Beat:              return 1.0;
    case SnapMode::HalfNote:          return 2.0;
    case SnapMode::Bar:               return double(timeSigNum_);
    }
    return 1.0;
}

double GridSnapper::snapBeat(double beat) const
{
    double interval = gridIntervalBeats();
    if (interval <= 0.0 || mode_ == SnapMode::Off)
        return beat;
    return std::max(0.0, std::round(beat / interval) * interval);
}

} // namespace OpenDaw
