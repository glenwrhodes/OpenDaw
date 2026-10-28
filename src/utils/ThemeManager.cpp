#include "ThemeManager.h"

namespace OpenDaw {

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

const std::vector<QColor>& ThemeManager::trackPalette()
{
    static const std::vector<QColor> palette = {
        {84, 150, 214},   // sky
        {214, 96, 92},    // red
        {96, 178, 112},   // green
        {222, 148, 72},   // orange
        {146, 110, 212},  // violet
        {214, 186, 72},   // gold
        {64, 170, 164},   // teal
        {204, 100, 172},  // magenta
        {150, 190, 82},   // lime
        {100, 116, 214},  // blue
        {214, 112, 140},  // rose
        {158, 138, 116},  // taupe
    };
    return palette;
}

} // namespace OpenDaw
