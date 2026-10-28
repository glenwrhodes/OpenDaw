#pragma once

#include <QColor>
#include <QString>
#include <QFont>
#include <vector>

namespace OpenDaw {

// Shared sizes so the track, folder and automation headers line up.
namespace metrics {
    constexpr int kTrackHeaderWidth = 220;
    constexpr int kTrackColourStrip = 5;
}

struct Theme {
    // Neutral graphite UI: dark separators between lighter panels, one blue accent.
    QColor background         {29, 30, 33};
    QColor surface            {40, 41, 45};
    QColor surfaceLight       {53, 55, 60};
    QColor border             {62, 64, 70};
    QColor separator          {16, 17, 19};
    QColor text               {222, 224, 228};
    QColor textDim            {142, 146, 154};
    QColor accent             {64, 140, 230};
    QColor accentLight        {122, 180, 246};
    QColor trackBackground    {34, 35, 39};
    QColor clipBody           {50, 130, 120, 180};
    QColor clipBodySelected   {70, 175, 160, 200};
    QColor waveform           {200, 222, 240};
    QColor playhead           {255, 72, 60};
    QColor gridLine           {42, 43, 48};
    QColor gridLineMajor      {60, 62, 68};
    QColor meterGreen         {72, 200, 96};
    QColor meterYellow        {232, 196, 60};
    QColor meterRed           {236, 64, 52};
    QColor meterGlow          {72, 200, 96, 50};
    QColor muteButton         {236, 140, 40};
    QColor soloButton         {232, 200, 48};
    QColor recordArm          {222, 52, 48};
    QColor transportPlay      {70, 200, 105};
    QColor transportStop      {200, 200, 200};
    QColor transportRecord    {222, 52, 48};
    QColor lcdText            {150, 228, 172};
    QColor lcdBackground      {13, 14, 16};

    QColor midiClipBody           {60, 95, 145, 180};
    QColor midiClipBodySelected   {80, 125, 180, 200};
    QColor midiNotePreview        {140, 190, 255};
    QColor pianoRollBackground    {22, 22, 28};
    QColor pianoRollNote          {100, 180, 255};
    QColor pianoRollNoteSelected  {255, 200, 80};
    QColor pianoRollBlackKey      {18, 18, 24};
    QColor pianoRollWhiteKey      {44, 46, 52};
    QColor pianoRollGrid          {36, 38, 44};
    QColor pianoKeyWhite          {215, 215, 218};
    QColor pianoKeyBlack          {34, 34, 40};
    QColor pianoKeyBorder         {160, 160, 160};
    QColor pianoRollVelocityBar   {100, 180, 255};
};

class ThemeManager {
public:
    static ThemeManager& instance();

    const Theme& current() const { return theme_; }
    void setCurrent(const Theme& t) { theme_ = t; }

    // Muted, distinct colours new tracks cycle through.
    static const std::vector<QColor>& trackPalette();

private:
    ThemeManager() = default;
    Theme theme_;
};

} // namespace OpenDaw
