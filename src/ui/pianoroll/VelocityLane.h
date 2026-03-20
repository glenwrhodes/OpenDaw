#pragma once

#include <QWidget>
#include <tracktion_engine/tracktion_engine.h>
#include <vector>
#include <set>

namespace te = tracktion::engine;

namespace freedaw {

class VelocityLane : public QWidget {
    Q_OBJECT

public:
    explicit VelocityLane(QWidget* parent = nullptr);

    void setClip(te::MidiClip* clip);
    void setClips(const std::vector<te::MidiClip*>& clips, const std::set<int>& hidden);
    void setPixelsPerBeat(double ppb) { pixelsPerBeat_ = ppb; update(); }
    void setScrollOffset(int x) { scrollOffset_ = x; update(); }
    void refresh();

signals:
    void velocityChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<te::MidiNote*> notesAtX(double x) const;

    std::vector<te::MidiClip*> allClips_;
    std::set<int> hiddenChannels_;
    te::MidiClip* primaryClip_ = nullptr;
    double pixelsPerBeat_ = 60.0;
    int scrollOffset_ = 0;
    std::vector<te::MidiNote*> draggingNotes_;
};

} // namespace freedaw
