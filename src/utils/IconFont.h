#pragma once

#include <QChar>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QString>

namespace OpenDaw {
namespace icons {

// ── Registration (call once at startup) ─────────────────────────────────────

inline bool registerFonts()
{
    bool ok = true;
    if (QFontDatabase::addApplicationFont(":/fontaudio.ttf") < 0)
        ok = false;
    if (QFontDatabase::addApplicationFont(":/MaterialSymbolsOutlined.ttf") < 0)
        ok = false;
    if (QFontDatabase::addApplicationFont(":/Bravura.otf") < 0)
        ok = false;
    return ok;
}

// ── Font accessors ──────────────────────────────────────────────────────────

inline QFont fontAudio(int pixelSize)
{
    QFont f("fontaudio");
    f.setPixelSize(pixelSize);
    return f;
}

inline QFont materialIcons(int pixelSize)
{
    QFont f("Material Icons Outlined");
    f.setPixelSize(pixelSize);
    return f;
}

inline QFont bravuraMusic(int pixelSize)
{
    QFont f("Bravura");
    f.setPixelSize(pixelSize);
    return f;
}

// ── Render a glyph as a QIcon (for use with QAction / QToolButton) ──────

inline QIcon glyphIcon(const QFont& font, const QChar& glyph,
                       const QColor& color, int size = 20)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QString(glyph));
    return QIcon(pix);
}

// ── FontAudio glyphs (from fontaudio Icons.h codepoints) ────────────────────

namespace fa {
    inline const QChar Play          {0xF169};
    inline const QChar Pause         {0xF166};
    inline const QChar Stop          {0xF18D};
    inline const QChar Record        {0xF176};
    inline const QChar Loop          {0xF155};
    inline const QChar Backward      {0xF114};
    inline const QChar Forward       {0xF13A};
    inline const QChar Rew           {0xF17A};
    inline const QChar Ffwd          {0xF12E};
    inline const QChar Next          {0xF163};
    inline const QChar Prev          {0xF170};
    inline const QChar Metronome     {0xF156};

    inline const QChar Scissors      {0xF17F};
    inline const QChar Cutter        {0xF11D};
    inline const QChar Copy          {0xF11B};
    inline const QChar Paste         {0xF165};
    inline const QChar Eraser        {0xF12D};
    inline const QChar Pen           {0xF167};
    inline const QChar Pointer       {0xF16A};
    inline const QChar Duplicate     {0xF12C};

    inline const QChar Mute          {0xF162};
    inline const QChar Solo          {0xF188};
    inline const QChar Mono          {0xF161};
    inline const QChar Stereo        {0xF18C};
    inline const QChar Speaker       {0xF189};
    inline const QChar Headphones    {0xF13E};
    inline const QChar Armrecording  {0xF105};

    inline const QChar Save          {0xF17D};
    inline const QChar SaveAs        {0xF17E};
    inline const QChar Open          {0xF164};
    inline const QChar Undo          {0xF190};
    inline const QChar Redo          {0xF177};
    inline const QChar ZoomIn        {0xF19A};
    inline const QChar ZoomOut       {0xF19B};
    inline const QChar Lock          {0xF140};
    inline const QChar Unlock        {0xF191};
    inline const QChar Waveform      {0xF198};
    inline const QChar Keyboard      {0xF13F};
    inline const QChar Cpu           {0xF11C};
    inline const QChar Drumpad       {0xF12B};
    inline const QChar Close         {0xF11A};
    inline const QChar Powerswitch   {0xF16B};
    inline const QChar Midiplug      {0xF158};
    inline const QChar Microphone    {0xF157};

    inline const QChar PunchIn       {0xF171};
    inline const QChar PunchOut      {0xF172};
    inline const QChar RepeatOne     {0xF178};
    inline const QChar Repeat        {0xF179};
    inline const QChar Shuffle       {0xF180};
    inline const QChar Timeselect    {0xF18F};

    inline const QChar HExpand       {0xF13B};
    inline const QChar VExpand       {0xF193};

    inline const QChar ArrowsHorz    {0xF10F};
    inline const QChar ArrowsVert    {0xF110};
}

// ── Material Icons Outlined glyphs ──────────────────────────────────────────

namespace mi {
    inline const QChar Add               {0xE145};
    inline const QChar Delete            {0xE872};
    inline const QChar Folder            {0xE2C7};
    inline const QChar InsertDriveFile   {0xE24D};
    inline const QChar CreateNewFolder   {0xE2CC};
    inline const QChar FileOpen          {0xEAF3};
    inline const QChar Tune              {0xE429};
    inline const QChar ContentCut        {0xF08B};
    inline const QChar NoteAdd           {0xE89C};
    inline const QChar LibraryMusic      {0xE030};
    inline const QChar QueueMusic        {0xE03D};
    inline const QChar PlaylistAdd       {0xE03B};
    inline const QChar Refresh           {0xE5D5};
    inline const QChar Settings          {0xE8B8};
    inline const QChar ShowChart         {0xE6E1};
    inline const QChar Visibility        {0xE8F4};
    inline const QChar VisibilityOff     {0xE8F5};
    inline const QChar ZoomIn            {0xE8FF};
    inline const QChar ZoomOut           {0xE900};
    inline const QChar Save              {0xE161};
    inline const QChar Undo              {0xE166};
    inline const QChar Redo              {0xE15A};
    inline const QChar Description       {0xE873};
    inline const QChar Chat              {0xE0B7};
    inline const QChar Warning           {0xE002};
}

} // namespace icons
} // namespace OpenDaw
