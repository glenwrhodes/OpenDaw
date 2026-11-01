#pragma once

#include <QWidget>
#include <functional>

namespace OpenDaw {

class TimeRuler : public QWidget {
    Q_OBJECT

public:
    explicit TimeRuler(QWidget* parent = nullptr);

    QSize sizeHint() const override { return {800, 28}; }

    void setPixelsPerBeat(double ppb) { pixelsPerBeat_ = ppb; update(); }
    void setScrollX(double sx) { scrollX_ = sx; update(); }
    void setBpm(double bpm) { bpm_ = bpm; update(); }
    void setTimeSig(int num, int den) { timeSigNum_ = num; timeSigDen_ = den; update(); }
    void setPlayheadBeat(double beat) { playheadBeat_ = beat; update(); }

    void setLoopRegion(double inBeat, double outBeat);
    void setLoopEnabled(bool enabled);
    bool loopEnabled() const { return loopEnabled_; }
    // Punch shares the loop range; while on, the range shows in the record colour.
    void setPunchEnabled(bool enabled);
    bool regionVisible() const;
    double loopInBeat() const { return loopInBeat_; }
    double loopOutBeat() const { return loopOutBeat_; }

    void setSnapFunction(std::function<double(double)> fn) { snapFn_ = std::move(fn); }

signals:
    void positionClicked(double beat);
    void positionDragged(double beat);
    void loopRegionChanged(double inBeat, double outBeat);
    void loopInRequested(double beat);
    void loopOutRequested(double beat);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    double xToBeat(int x) const;
    double beatToX(double beat) const;
    static constexpr double kHandleHitRadius = 6.0;

    double pixelsPerBeat_ = 40.0;
    double scrollX_       = 0.0;
    double bpm_           = 120.0;
    int    timeSigNum_    = 4;
    int    timeSigDen_    = 4;
    double playheadBeat_  = 0.0;
    bool   dragging_      = false;

    double loopInBeat_  = 0.0;
    double loopOutBeat_ = 0.0;
    bool   loopEnabled_ = false;
    bool   punchEnabled_ = false;
    bool   draggingLoopIn_  = false;
    bool   draggingLoopOut_ = false;

    std::function<double(double)> snapFn_;
};

} // namespace OpenDaw
