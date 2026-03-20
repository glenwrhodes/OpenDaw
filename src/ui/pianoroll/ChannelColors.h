#pragma once

#include <QColor>

namespace freedaw {

inline const QColor kChannelColors[16] = {
    {100, 180, 255},  // Ch1  Blue
    {255, 140, 90},   // Ch2  Coral
    {120, 220, 120},  // Ch3  Green
    {255, 200, 80},   // Ch4  Gold
    {180, 130, 230},  // Ch5  Lavender
    {255, 110, 160},  // Ch6  Pink
    {80, 210, 210},   // Ch7  Teal
    {220, 160, 100},  // Ch8  Tan
    {140, 200, 80},   // Ch9  Lime
    {200, 100, 140},  // Ch10 Mauve
    {100, 160, 200},  // Ch11 Steel
    {230, 180, 140},  // Ch12 Peach
    {160, 210, 160},  // Ch13 Sage
    {200, 150, 180},  // Ch14 Rose
    {180, 200, 120},  // Ch15 Olive
    {160, 180, 220},  // Ch16 Periwinkle
};

inline QColor channelColor(int ch)
{
    if (ch < 1 || ch > 16) return kChannelColors[0];
    return kChannelColors[ch - 1];
}

} // namespace freedaw
