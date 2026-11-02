#pragma once

#include <QWidget>

namespace OpenDaw {

class PianoKeyboard : public QWidget {
    Q_OBJECT

public:
    explicit PianoKeyboard(QWidget* parent = nullptr);

    void setNoteRowHeight(double h) { noteRowHeight_ = h; update(); }
    double noteRowHeight() const { return noteRowHeight_; }

    void setScrollOffset(int y) { scrollOffset_ = y; update(); }

    // Label every key with its General MIDI drum sound (for drum kits).
    void setDrumNames(bool on);
    bool drumNames() const { return drumNames_; }
    int keyWidth() const { return drumNames_ ? DRUM_KEY_WIDTH : KEY_WIDTH; }
    static QString drumName(int note);   // empty outside the drum map

    QSize sizeHint() const override;

signals:
    void keyWidthChanged(int width);
    void noteClicked(int noteNumber);
    void noteReleased(int noteNumber);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    static bool isBlackKey(int note);
    static QString noteName(int note);
    int noteFromY(double y) const;

    double noteRowHeight_ = 14.0;
    int scrollOffset_ = 0;
    int pressedNote_ = -1;
    bool drumNames_ = false;
    static constexpr int TOTAL_NOTES = 128;
    static constexpr int KEY_WIDTH = 60;
    static constexpr int DRUM_KEY_WIDTH = 132;
};

} // namespace OpenDaw
