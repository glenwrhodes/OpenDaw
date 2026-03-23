#pragma once

namespace OpenDaw {

enum class SnapMode {
    Off,
    EighthBeat,       // 1/32 note  = 0.125 beats
    QuarterBeat,      // 1/16 note  = 0.25  beats
    TripletSixteenth, // 1/16 triplet = 1/6 beat ≈ 0.167
    HalfBeat,         // 1/8  note  = 0.5   beats
    TripletEighth,    // 1/8  triplet = 1/3 beat ≈ 0.333
    Beat,             // 1/4  note  = 1.0   beat
    TripletQuarter,   // 1/4  triplet = 2/3 beat ≈ 0.667
    HalfNote,         // 1/2  note  = 2.0   beats
    Bar               // 1 bar      = timeSigNum beats
};

class GridSnapper {
public:
    void setMode(SnapMode mode) { mode_ = mode; }
    SnapMode mode() const { return mode_; }

    void setBpm(double bpm)      { bpm_ = bpm; }
    void setTimeSig(int num, int den) { timeSigNum_ = num; timeSigDen_ = den; }

    double snapBeat(double beat) const;
    double gridIntervalBeats() const;

private:
    SnapMode mode_ = SnapMode::Beat;
    double   bpm_  = 120.0;
    int      timeSigNum_ = 4;
    int      timeSigDen_ = 4;
};

} // namespace OpenDaw
