#include "PianoKeyboard.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>

namespace freedaw {

PianoKeyboard::PianoKeyboard(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Piano Keyboard");
    setFixedWidth(KEY_WIDTH);
}

QSize PianoKeyboard::sizeHint() const
{
    return QSize(KEY_WIDTH, static_cast<int>(TOTAL_NOTES * noteRowHeight_));
}

bool PianoKeyboard::isBlackKey(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

QString PianoKeyboard::noteName(int note)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (note / 12) - 2;
    return QString("%1%2").arg(names[note % 12]).arg(octave);
}

int PianoKeyboard::noteFromY(double y) const
{
    double adjustedY = y + scrollOffset_;
    int row = static_cast<int>(adjustedY / noteRowHeight_);
    return (TOTAL_NOTES - 1) - row;
}

void PianoKeyboard::paintEvent(QPaintEvent*)
{
    auto& theme = ThemeManager::instance().current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int h = height();

    for (int note = 0; note < TOTAL_NOTES; ++note) {
        int row = (TOTAL_NOTES - 1) - note;
        double y = row * noteRowHeight_ - scrollOffset_;

        if (y + noteRowHeight_ < 0 || y > h) continue;

        QRectF r(0, y, KEY_WIDTH, noteRowHeight_);

        bool black = isBlackKey(note);
        if (black) {
            painter.fillRect(r, theme.pianoKeyBlack);
        } else {
            painter.fillRect(r, theme.pianoKeyWhite);
        }

        painter.setPen(QPen(theme.pianoKeyBorder, 0.5));
        painter.drawLine(QPointF(0, y + noteRowHeight_),
                         QPointF(KEY_WIDTH, y + noteRowHeight_));

        if (note % 12 == 0) {
            painter.setPen(black ? Qt::white : QColor(40, 40, 40));
            QFont f = painter.font();
            f.setPixelSize(std::max(8, static_cast<int>(noteRowHeight_ - 3)));
            f.setBold(true);
            painter.setFont(f);
            painter.drawText(r.adjusted(3, 0, 0, 0),
                             Qt::AlignVCenter | Qt::AlignLeft,
                             noteName(note));
        }
    }

    painter.setPen(QPen(theme.pianoKeyBorder, 1.0));
    painter.drawLine(KEY_WIDTH - 1, 0, KEY_WIDTH - 1, h);
}

void PianoKeyboard::mousePressEvent(QMouseEvent* event)
{
    int note = noteFromY(event->position().y());
    if (note >= 0 && note < TOTAL_NOTES)
        emit noteClicked(note);
}

} // namespace freedaw
