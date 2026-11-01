#include "EditTool.h"
#include "utils/IconFont.h"
#include <QPainter>
#include <QPixmap>

namespace OpenDaw {

namespace {
EditTool gTool = EditTool::Smart;
bool gRipple = false;

QCursor glyphCursor(const QFont& font, const QChar& glyph, int hotX, int hotY)
{
    constexpr int size = 20;
    // White glyph over a dark outline reads on any theme.
    QPixmap pm(size + 2, size + 2);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        const QPixmap outline = icons::glyphIcon(font, glyph, QColor(0, 0, 0, 200), size).pixmap(size, size);
        for (int dx = 0; dx <= 2; ++dx)
            for (int dy = 0; dy <= 2; ++dy)
                p.drawPixmap(dx, dy, outline);
        p.drawPixmap(1, 1, icons::glyphIcon(font, glyph, Qt::white, size).pixmap(size, size));
    }
    return QCursor(pm, hotX, hotY);
}
}

EditTool activeEditTool() { return gTool; }
void setActiveEditTool(EditTool tool) { gTool = tool; }
bool rippleEditing() { return gRipple; }
void setRippleEditing(bool on) { gRipple = on; }

QCursor cursorForTool(EditTool tool)
{
    switch (tool) {
        case EditTool::Range:  return QCursor(Qt::IBeamCursor);
        case EditTool::Razor: {
            static const QCursor c = glyphCursor(icons::fontAudio(20), icons::fa::Scissors, 11, 11);
            return c;
        }
        case EditTool::Pen: {
            static const QCursor c = glyphCursor(icons::fontAudio(20), icons::fa::Pen, 2, 20);
            return c;
        }
        case EditTool::Eraser: {
            static const QCursor c = glyphCursor(icons::fontAudio(20), icons::fa::Eraser, 4, 18);
            return c;
        }
        case EditTool::Smart:
            break;
    }
    return QCursor(Qt::ArrowCursor);
}

} // namespace OpenDaw
