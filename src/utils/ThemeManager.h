#pragma once

#include <QColor>
#include <QString>
#include <QFont>

namespace OpenDaw {

struct Theme {
    QColor background         {24, 24, 26};
    QColor surface            {32, 33, 36};
    QColor surfaceLight       {48, 50, 54};
    QColor border             {52, 54, 58};
    QColor text               {225, 228, 232};
    QColor textDim            {130, 134, 140};
    QColor accent             {0, 168, 150};
    QColor accentLight        {40, 210, 190};
    QColor trackBackground    {28, 28, 32};
    QColor clipBody           {50, 130, 120, 180};
    QColor clipBodySelected   {70, 175, 160, 200};
    QColor waveform           {170, 240, 228};
    QColor playhead           {255, 80, 60};
    QColor gridLine           {40, 42, 46};
    QColor gridLineMajor      {56, 58, 62};
    QColor meterGreen         {0, 190, 170};
    QColor meterYellow        {0, 220, 200};
    QColor meterRed           {244, 67, 54};
    QColor meterGlow          {0, 255, 230, 60};
    QColor muteButton         {255, 152, 0};
    QColor soloButton         {255, 235, 59};
    QColor recordArm          {244, 67, 54};
    QColor transportPlay      {76, 200, 100};
    QColor transportStop      {200, 200, 200};
    QColor transportRecord    {244, 67, 54};

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

private:
    ThemeManager() = default;
    Theme theme_;
};

} // namespace OpenDaw
