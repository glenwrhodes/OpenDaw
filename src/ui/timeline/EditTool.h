#pragma once

#include <QCursor>

namespace OpenDaw {

// The timeline's edit tools. Smart is the pointer: move clips, trim their
// edges, drag fades, Alt+drag to slip the audio inside a clip, and drag on
// empty space to select a time range.
enum class EditTool { Smart, Range, Razor, Pen, Eraser };

// One timeline, so one active tool, shared by the scene and its clip items.
EditTool activeEditTool();
void setActiveEditTool(EditTool tool);
QCursor cursorForTool(EditTool tool);

// Ripple editing: deleting clips or a range closes the gap on their tracks.
bool rippleEditing();
void setRippleEditing(bool on);

} // namespace OpenDaw
