#pragma once

#include <QWidget>

namespace freedaw {

class PianoKeyboard : public QWidget {
    Q_OBJECT

public:
    explicit PianoKeyboard(QWidget* parent = nullptr);

    void setNoteRowHeight(double h) { noteRowHeight_ = h; update(); }
    double noteRowHeight() const { return noteRowHeight_; }

    void setScrollOffset(int y) { scrollOffset_ = y; update(); }

    QSize sizeHint() const override;

signals:
    void noteClicked(int noteNumber);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    static bool isBlackKey(int note);
    static QString noteName(int note);
    int noteFromY(double y) const;

    double noteRowHeight_ = 14.0;
    int scrollOffset_ = 0;
    static constexpr int TOTAL_NOTES = 128;
    static constexpr int KEY_WIDTH = 60;
};

} // namespace freedaw
