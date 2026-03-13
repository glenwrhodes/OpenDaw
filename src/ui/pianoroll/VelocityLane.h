#pragma once

#include <QWidget>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>

namespace te = tracktion::engine;

namespace freedaw {

class VelocityLane : public QWidget {
    Q_OBJECT

public:
    explicit VelocityLane(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip);
    void setPixelsPerBeat(double ppb) { pixelsPerBeat_ = ppb; update(); }
    void setScrollOffset(int x) { scrollOffset_ = x; update(); }
    void refresh();

signals:
    void velocityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    te::MidiNote* noteAtX(double x) const;

    te::MidiClip* clip_ = nullptr;
    double pixelsPerBeat_ = 60.0;
    int scrollOffset_ = 0;
    te::MidiNote* draggingNote_ = nullptr;
};

} // namespace freedaw
