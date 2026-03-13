#pragma once

#include <QColor>
#include <QString>
#include <QFont>

namespace freedaw {

struct Theme {
    QColor background         {30, 30, 30};
    QColor surface            {42, 42, 42};
    QColor surfaceLight       {55, 55, 55};
    QColor border             {70, 70, 70};
    QColor text               {220, 220, 220};
    QColor textDim            {140, 140, 140};
    QColor accent             {0, 150, 136};
    QColor accentLight        {38, 198, 183};
    QColor trackBackground    {35, 35, 38};
    QColor clipBody           {60, 120, 110};
    QColor clipBodySelected   {80, 160, 148};
    QColor waveform           {160, 230, 220};
    QColor playhead           {255, 80, 60};
    QColor gridLine           {50, 50, 50};
    QColor gridLineMajor      {65, 65, 65};
    QColor meterGreen         {76, 175, 80};
    QColor meterYellow        {255, 193, 7};
    QColor meterRed           {244, 67, 54};
    QColor muteButton         {255, 152, 0};
    QColor soloButton         {255, 235, 59};
    QColor recordArm          {244, 67, 54};
    QColor transportPlay      {76, 175, 80};
    QColor transportStop      {200, 200, 200};
    QColor transportRecord    {244, 67, 54};

    QColor midiClipBody           {70, 100, 140};
    QColor midiClipBodySelected   {90, 130, 175};
    QColor midiNotePreview        {140, 190, 255};
    QColor pianoRollBackground    {25, 25, 30};
    QColor pianoRollNote          {100, 180, 255};
    QColor pianoRollNoteSelected  {255, 200, 80};
    QColor pianoRollBlackKey      {20, 20, 25};
    QColor pianoRollWhiteKey      {50, 50, 55};
    QColor pianoRollGrid          {40, 40, 45};
    QColor pianoKeyWhite          {230, 230, 230};
    QColor pianoKeyBlack          {30, 30, 30};
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

} // namespace freedaw
