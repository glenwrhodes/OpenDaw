#include "PianoKeyboard.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QMouseEvent>

namespace OpenDaw {

PianoKeyboard::PianoKeyboard(QWidget* parent)
    : QWidget(parent)
{
    setAccessibleName("Piano Keyboard");
    setFixedWidth(KEY_WIDTH);
}

QSize PianoKeyboard::sizeHint() const
{
    return QSize(keyWidth(), static_cast<int>(TOTAL_NOTES * noteRowHeight_));
}

void PianoKeyboard::setDrumNames(bool on)
{
    if (on == drumNames_) return;
    drumNames_ = on;
    setFixedWidth(keyWidth());
    update();
    emit keyWidthChanged(keyWidth());
}

QString PianoKeyboard::drumName(int note)
{
    // General MIDI percussion (35-81) plus the GS extras most kits include.
    static const char* names[] = {
        "High Q", "Slap", "Scratch Push", "Scratch Pull", "Sticks",           // 27
        "Square Click", "Metronome Click", "Metronome Bell",                   // 32
        "Kick 2", "Kick", "Side Stick", "Snare", "Hand Clap", "Snare 2",       // 35
        "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat",    // 41
        "Low Tom", "Open Hi-Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash 1",      // 45
        "High Tom", "Ride 1", "China", "Ride Bell", "Tambourine", "Splash",    // 50
        "Cowbell", "Crash 2", "Vibraslap", "Ride 2", "Hi Bongo", "Low Bongo",  // 56
        "Mute Hi Conga", "Open Hi Conga", "Low Conga", "High Timbale",         // 62
        "Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas",         // 66
        "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves",// 71
        "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica",         // 76
        "Mute Triangle", "Open Triangle", "Shaker", "Jingle Bell", "Bell Tree",// 80
        "Castanets", "Mute Surdo", "Open Surdo"                                // 85
    };
    constexpr int first = 27;
    constexpr int count = int(sizeof(names) / sizeof(names[0]));
    if (note < first || note >= first + count) return {};
    return names[note - first];
}

bool PianoKeyboard::isBlackKey(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

QString PianoKeyboard::noteName(int note)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (note / 12) - 1;
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

        QRectF r(0, y, keyWidth(), noteRowHeight_);

        bool black = isBlackKey(note);

        if (drumNames_) {
            // A list of drum sounds: light rows (black-key notes a shade darker),
            // the sound's name, and the note name on each C for reference.
            const QString name = drumName(note);
            QColor rowColor = black ? theme.pianoKeyWhite.darker(112) : theme.pianoKeyWhite;
            painter.fillRect(r, rowColor);
            painter.setPen(QPen(QColor(175, 175, 175), 1.0));
            painter.drawLine(QPointF(r.left(), r.bottom() - 1), QPointF(r.right() - 1, r.bottom() - 1));

            QFont f = painter.font();
            f.setPixelSize(std::max(8, static_cast<int>(noteRowHeight_ - 4)));
            f.setBold(false);
            painter.setFont(f);
            if (!name.isEmpty()) {
                painter.setPen(QColor(30, 30, 30));
                painter.drawText(r.adjusted(4, 0, -26, 0), Qt::AlignVCenter | Qt::AlignLeft, name);
            }
            if (note % 12 == 0 || name.isEmpty()) {
                painter.setPen(QColor(120, 120, 120));
                painter.drawText(r.adjusted(0, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight, noteName(note));
            }
            continue;
        }

        QColor baseColor = black ? theme.pianoKeyBlack : theme.pianoKeyWhite;
        QColor highlight = black ? QColor(70, 70, 70) : QColor(255, 255, 255);
        QColor shadow    = black ? QColor(10, 10, 10)  : QColor(175, 175, 175);

        painter.fillRect(r, baseColor);

        painter.setPen(QPen(highlight, 1.0));
        painter.drawLine(QPointF(r.left(), r.top()), QPointF(r.right() - 1, r.top()));
        painter.drawLine(QPointF(r.left(), r.top()), QPointF(r.left(), r.bottom() - 1));

        painter.setPen(QPen(shadow, 1.0));
        painter.drawLine(QPointF(r.left(), r.bottom() - 1), QPointF(r.right() - 1, r.bottom() - 1));
        painter.drawLine(QPointF(r.right() - 1, r.top()), QPointF(r.right() - 1, r.bottom() - 1));

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
    painter.drawLine(keyWidth() - 1, 0, keyWidth() - 1, h);
}

void PianoKeyboard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    int note = noteFromY(event->position().y());
    if (note >= 0 && note < TOTAL_NOTES) {
        pressedNote_ = note;
        emit noteClicked(note);
    }
}

void PianoKeyboard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (pressedNote_ >= 0) {
        emit noteReleased(pressedNote_);
        pressedNote_ = -1;
    }
}

void PianoKeyboard::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || pressedNote_ < 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    int note = noteFromY(event->position().y());
    note = std::clamp(note, 0, TOTAL_NOTES - 1);
    if (note != pressedNote_) {
        emit noteReleased(pressedNote_);
        pressedNote_ = note;
        emit noteClicked(note);
    }
}

void PianoKeyboard::leaveEvent(QEvent* event)
{
    if (pressedNote_ >= 0) {
        emit noteReleased(pressedNote_);
        pressedNote_ = -1;
    }
    QWidget::leaveEvent(event);
}

} // namespace OpenDaw
