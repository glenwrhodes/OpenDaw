#include "ThemeManager.h"
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>

namespace OpenDaw {

namespace {

QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor(int(a.red() * t + b.red() * (1.0 - t)),
                  int(a.green() * t + b.green() * (1.0 - t)),
                  int(a.blue() * t + b.blue() * (1.0 - t)));
}

QColor withAlpha(const QColor& c, int alpha)
{
    return QColor(c.red(), c.green(), c.blue(), alpha);
}

// Fills the colours every theme derives from its core palette. Call after the
// core colours are set, then override anything a theme wants to be specific about.
void finish(Theme& t)
{
    t.trackBackground = mix(t.background, t.surface, 0.55);
    t.gridLine = mix(t.border, t.trackBackground, 0.30);
    t.gridLineMajor = mix(t.border, t.trackBackground, 0.70);
    t.meterGlow = withAlpha(t.meterGreen, 50);
    t.transportRecord = t.recordArm;
    t.clipBody = withAlpha(t.accent, 180);
    t.clipBodySelected = withAlpha(t.accentLight, 200);
    t.waveform = t.isLight ? t.accent.darker(140) : t.accentLight;
    t.midiClipBody = withAlpha(t.accent, 180);
    t.midiClipBodySelected = withAlpha(t.accentLight, 200);
    t.midiNotePreview = t.accentLight;
    t.pianoRollBackground = t.background;
    t.pianoRollWhiteKey = t.isLight ? t.surfaceLight : mix(t.surfaceLight, t.background, 0.55);
    t.pianoRollBlackKey = t.isLight ? t.surface.darker(104) : t.background.darker(115);
    t.pianoRollGrid = t.gridLine;
    t.pianoRollNote = t.accent;
    t.pianoRollVelocityBar = t.accent;
}

std::vector<Theme> makeThemes()
{
    std::vector<Theme> themes;

    {   // The default: neutral graphite with a blue accent.
        Theme t;
        t.name = "Studio Graphite";
        t.trackColours = {
            {84, 150, 214}, {214, 96, 92}, {96, 178, 112}, {222, 148, 72},
            {146, 110, 212}, {214, 186, 72}, {64, 170, 164}, {204, 100, 172},
            {150, 190, 82}, {100, 116, 214}, {214, 112, 140}, {158, 138, 116},
        };
        themes.push_back(t);   // keeps its hand-tuned values; no finish()
    }

    {   // Sci-fi: cold blue-greys with teal light.
        Theme t;
        t.name = "Nebula Station";
        t.background = {13, 19, 27};   t.surface = {21, 30, 41};   t.surfaceLight = {31, 44, 59};
        t.border = {45, 64, 82};       t.separator = {6, 10, 15};
        t.text = {204, 228, 238};      t.textDim = {112, 142, 160};
        t.accent = {0, 196, 204};      t.accentLight = {110, 236, 240};
        t.playhead = {255, 80, 110};
        t.lcdText = {90, 240, 230};    t.lcdBackground = {3, 9, 13};
        t.controlBody = {58, 74, 92};  t.controlTrack = {7, 12, 18};  t.controlHandle = {170, 205, 220};
        t.meterGreen = {0, 210, 170};  t.meterYellow = {230, 210, 80}; t.meterRed = {255, 70, 90};
        t.trackColours = {
            {0, 190, 200}, {70, 140, 230}, {130, 110, 230}, {0, 210, 150},
            {90, 180, 255}, {200, 110, 240}, {255, 120, 170}, {240, 190, 80},
            {120, 220, 220}, {100, 160, 190}, {230, 120, 90}, {160, 200, 120},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Cream walls, walnut and brass: warm and friendly, light.
        Theme t;
        t.name = "Conservatory";
        t.isLight = true;
        t.background = {243, 236, 222}; t.surface = {233, 224, 206}; t.surfaceLight = {251, 247, 239};
        t.border = {201, 186, 161};     t.separator = {176, 160, 134};
        t.text = {60, 48, 38};          t.textDim = {128, 112, 94};
        t.accent = {176, 104, 48};      t.accentLight = {208, 148, 86};
        t.playhead = {196, 48, 48};
        t.lcdText = {250, 220, 160};    t.lcdBackground = {62, 44, 32};
        t.controlBody = {240, 232, 216}; t.controlTrack = {214, 202, 180}; t.controlHandle = {120, 90, 64};
        t.meterGreen = {70, 160, 80};   t.meterYellow = {214, 170, 40}; t.meterRed = {200, 60, 50};
        t.muteButton = {214, 130, 40};  t.soloButton = {214, 178, 40};  t.recordArm = {196, 52, 48};
        t.transportPlay = {60, 150, 80}; t.transportStop = {110, 96, 80};
        t.trackColours = {
            {170, 98, 60}, {120, 150, 90}, {196, 120, 80}, {90, 130, 160},
            {200, 160, 70}, {150, 90, 120}, {100, 150, 140}, {190, 110, 110},
            {130, 110, 90}, {150, 130, 190}, {80, 120, 80}, {210, 140, 100},
        };
        finish(t);
        t.pianoKeyWhite = {253, 250, 244};
        t.pianoKeyBlack = {44, 34, 28};
        t.pianoKeyBorder = {170, 155, 130};
        t.pianoRollNoteSelected = {200, 70, 50};
        themes.push_back(t);
    }

    {   // Green-on-black terminal glow.
        Theme t;
        t.name = "Code Rain";
        t.background = {2, 7, 3};      t.surface = {5, 14, 7};     t.surfaceLight = {10, 28, 14};
        t.border = {18, 58, 28};       t.separator = {0, 3, 1};
        t.text = {110, 255, 140};      t.textDim = {40, 150, 70};
        t.accent = {0, 220, 90};       t.accentLight = {140, 255, 170};
        t.playhead = {210, 255, 215};
        t.lcdText = {0, 255, 100};     t.lcdBackground = {0, 5, 2};
        t.controlBody = {20, 48, 26};  t.controlTrack = {0, 8, 3};  t.controlHandle = {90, 220, 120};
        t.meterGreen = {0, 220, 90};   t.meterYellow = {180, 255, 90}; t.meterRed = {255, 80, 60};
        t.muteButton = {230, 170, 40}; t.soloButton = {200, 255, 80}; t.recordArm = {255, 70, 60};
        t.transportPlay = {0, 230, 90}; t.transportStop = {120, 220, 140};
        t.trackColours = {
            {0, 200, 80}, {60, 230, 120}, {0, 160, 90}, {120, 240, 100},
            {0, 220, 160}, {80, 180, 60}, {30, 140, 60}, {150, 255, 170},
            {0, 180, 130}, {100, 200, 140}, {170, 230, 80}, {40, 110, 60},
        };
        finish(t);
        t.pianoRollNoteSelected = {220, 255, 120};
        t.pianoKeyWhite = {150, 230, 160};
        t.pianoKeyBlack = {4, 20, 8};
        t.pianoKeyBorder = {20, 90, 40};
        themes.push_back(t);
    }

    {   // Navy desk with amber lamps.
        Theme t;
        t.name = "Midnight Amber";
        t.background = {14, 16, 24};   t.surface = {22, 25, 36};   t.surfaceLight = {32, 36, 50};
        t.border = {48, 53, 70};       t.separator = {6, 7, 12};
        t.text = {226, 222, 210};      t.textDim = {140, 138, 130};
        t.accent = {240, 166, 56};     t.accentLight = {255, 204, 120};
        t.playhead = {255, 90, 70};
        t.lcdText = {255, 190, 90};    t.lcdBackground = {8, 8, 12};
        t.controlBody = {66, 70, 88};  t.controlTrack = {8, 9, 14};  t.controlHandle = {214, 206, 190};
        t.trackColours = {
            {240, 166, 56}, {90, 150, 220}, {220, 96, 80}, {120, 190, 140},
            {180, 130, 220}, {230, 210, 110}, {80, 190, 200}, {220, 130, 170},
            {160, 170, 90}, {130, 140, 220}, {230, 140, 90}, {150, 150, 150},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Neon magenta and cyan on deep purple.
        Theme t;
        t.name = "Synthwave";
        t.background = {22, 12, 34};   t.surface = {34, 19, 52};   t.surfaceLight = {50, 28, 74};
        t.border = {78, 46, 110};      t.separator = {12, 5, 20};
        t.text = {245, 225, 255};      t.textDim = {170, 140, 200};
        t.accent = {255, 56, 170};     t.accentLight = {255, 140, 220};
        t.playhead = {80, 230, 255};
        t.lcdText = {90, 240, 255};    t.lcdBackground = {10, 3, 18};
        t.controlBody = {84, 52, 120}; t.controlTrack = {12, 5, 20}; t.controlHandle = {240, 200, 255};
        t.meterGreen = {80, 230, 255}; t.meterYellow = {255, 140, 220}; t.meterRed = {255, 60, 90};
        t.trackColours = {
            {255, 56, 170}, {80, 230, 255}, {170, 90, 255}, {255, 190, 60},
            {60, 255, 180}, {255, 110, 90}, {120, 140, 255}, {230, 90, 255},
            {255, 230, 90}, {90, 200, 255}, {255, 130, 200}, {140, 255, 110},
        };
        finish(t);
        t.pianoRollNoteSelected = {80, 230, 255};
        themes.push_back(t);
    }

    {   // Cool, bright daylight.
        Theme t;
        t.name = "Arctic Daylight";
        t.isLight = true;
        t.background = {233, 237, 242}; t.surface = {222, 228, 236}; t.surfaceLight = {246, 248, 251};
        t.border = {190, 199, 211};     t.separator = {170, 180, 194};
        t.text = {30, 38, 50};          t.textDim = {102, 113, 128};
        t.accent = {36, 118, 214};      t.accentLight = {92, 160, 236};
        t.playhead = {220, 50, 60};
        t.lcdText = {150, 220, 255};    t.lcdBackground = {28, 36, 48};
        t.controlBody = {236, 240, 245}; t.controlTrack = {200, 208, 220}; t.controlHandle = {70, 82, 100};
        t.meterGreen = {40, 170, 90};   t.meterYellow = {220, 170, 30}; t.meterRed = {210, 50, 50};
        t.muteButton = {220, 130, 30};  t.soloButton = {214, 170, 20};  t.recordArm = {210, 50, 50};
        t.transportPlay = {40, 160, 80}; t.transportStop = {90, 100, 116};
        t.trackColours = {
            {36, 118, 214}, {220, 80, 70}, {40, 160, 100}, {230, 140, 40},
            {130, 90, 210}, {200, 160, 30}, {30, 160, 170}, {200, 80, 150},
            {110, 160, 50}, {80, 100, 220}, {210, 100, 120}, {120, 120, 130},
        };
        finish(t);
        t.pianoKeyWhite = {252, 253, 255};
        t.pianoKeyBlack = {40, 46, 56};
        t.pianoKeyBorder = {170, 178, 190};
        t.pianoRollNoteSelected = {230, 120, 30};
        themes.push_back(t);
    }

    {   // Warm brown analogue desk with VFD readouts.
        Theme t;
        t.name = "Vintage Console";
        t.background = {30, 25, 21};   t.surface = {44, 37, 31};   t.surfaceLight = {60, 51, 43};
        t.border = {84, 72, 60};       t.separator = {14, 11, 9};
        t.text = {236, 224, 204};      t.textDim = {160, 146, 126};
        t.accent = {232, 140, 48};     t.accentLight = {250, 186, 110};
        t.playhead = {255, 96, 60};
        t.lcdText = {140, 236, 200};   t.lcdBackground = {10, 12, 10};
        t.controlBody = {96, 84, 70};  t.controlTrack = {14, 11, 9}; t.controlHandle = {230, 220, 200};
        t.meterGreen = {120, 200, 80}; t.meterYellow = {240, 200, 70}; t.meterRed = {240, 80, 50};
        t.trackColours = {
            {232, 140, 48}, {200, 90, 60}, {120, 170, 110}, {214, 190, 110},
            {110, 150, 190}, {180, 110, 150}, {150, 190, 170}, {220, 160, 120},
            {170, 150, 90}, {130, 120, 170}, {200, 120, 90}, {150, 140, 130},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Charcoal with glowing red-orange.
        Theme t;
        t.name = "Ember";
        t.background = {20, 17, 17};   t.surface = {32, 27, 27};   t.surfaceLight = {46, 39, 38};
        t.border = {68, 56, 54};       t.separator = {9, 7, 7};
        t.text = {236, 226, 222};      t.textDim = {156, 140, 136};
        t.accent = {232, 80, 50};      t.accentLight = {255, 140, 100};
        t.playhead = {255, 200, 80};
        t.lcdText = {255, 130, 80};    t.lcdBackground = {10, 6, 6};
        t.controlBody = {80, 66, 64};  t.controlTrack = {9, 7, 7};  t.controlHandle = {226, 214, 208};
        t.trackColours = {
            {232, 80, 50}, {240, 150, 50}, {220, 190, 80}, {200, 70, 110},
            {120, 170, 200}, {160, 110, 200}, {110, 180, 120}, {240, 110, 90},
            {190, 140, 100}, {230, 130, 160}, {150, 160, 170}, {200, 170, 140},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Mossy greens and bark browns.
        Theme t;
        t.name = "Forest Canopy";
        t.background = {18, 24, 20};   t.surface = {28, 36, 30};   t.surfaceLight = {40, 51, 43};
        t.border = {58, 72, 60};       t.separator = {9, 13, 10};
        t.text = {226, 234, 222};      t.textDim = {140, 156, 138};
        t.accent = {120, 184, 92};     t.accentLight = {176, 220, 140};
        t.playhead = {255, 150, 80};
        t.lcdText = {200, 236, 150};   t.lcdBackground = {7, 11, 8};
        t.controlBody = {70, 86, 72};  t.controlTrack = {9, 13, 10}; t.controlHandle = {214, 224, 206};
        t.trackColours = {
            {120, 184, 92}, {196, 150, 90}, {90, 150, 130}, {210, 120, 80},
            {150, 130, 180}, {200, 190, 100}, {110, 140, 190}, {180, 100, 100},
            {160, 190, 140}, {140, 110, 80}, {90, 170, 170}, {220, 170, 120},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Soft slate with peach and pastels.
        Theme t;
        t.name = "Slate Dusk";
        t.background = {30, 33, 41};   t.surface = {40, 44, 55};   t.surfaceLight = {54, 59, 73};
        t.border = {72, 78, 96};       t.separator = {18, 20, 26};
        t.text = {228, 230, 238};      t.textDim = {148, 152, 170};
        t.accent = {240, 150, 120};    t.accentLight = {255, 190, 165};
        t.playhead = {255, 100, 110};
        t.lcdText = {255, 200, 170};   t.lcdBackground = {16, 18, 24};
        t.controlBody = {80, 86, 104}; t.controlTrack = {18, 20, 26}; t.controlHandle = {220, 222, 232};
        t.trackColours = {
            {240, 150, 120}, {130, 170, 230}, {170, 210, 150}, {220, 160, 220},
            {240, 210, 130}, {130, 210, 210}, {200, 140, 160}, {160, 150, 230},
            {220, 180, 150}, {150, 190, 200}, {230, 130, 140}, {180, 180, 190},
        };
        finish(t);
        themes.push_back(t);
    }

    {   // Maximum legibility: black, white and yellow.
        Theme t;
        t.name = "High Contrast";
        t.background = {0, 0, 0};      t.surface = {16, 16, 16};   t.surfaceLight = {40, 40, 40};
        t.border = {140, 140, 140};    t.separator = {90, 90, 90};
        t.text = {255, 255, 255};      t.textDim = {210, 210, 210};
        t.accent = {255, 214, 0};      t.accentLight = {255, 235, 120};
        t.playhead = {255, 60, 60};
        t.lcdText = {255, 255, 255};   t.lcdBackground = {0, 0, 0};
        t.controlBody = {90, 90, 90};  t.controlTrack = {30, 30, 30}; t.controlHandle = {255, 255, 255};
        t.trackColours = {
            {0, 170, 255}, {255, 80, 80}, {0, 220, 110}, {255, 170, 0},
            {190, 110, 255}, {255, 230, 0}, {0, 230, 230}, {255, 90, 200},
            {170, 255, 60}, {120, 140, 255}, {255, 140, 120}, {220, 220, 220},
        };
        finish(t);
        t.pianoRollNoteSelected = {0, 220, 255};
        themes.push_back(t);
    }

    return themes;
}

const char* kThemeSettingsKey = "ui/theme";

} // namespace

ThemeManager::ThemeManager()
    : theme_(builtInThemes().front())
{
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager mgr;
    return mgr;
}

const std::vector<Theme>& ThemeManager::builtInThemes()
{
    static const std::vector<Theme> themes = makeThemes();
    return themes;
}

bool ThemeManager::setCurrentByName(const QString& name)
{
    for (const auto& t : builtInThemes()) {
        if (t.name.compare(name, Qt::CaseInsensitive) == 0) {
            theme_ = t;
            return true;
        }
    }
    return false;
}

void ThemeManager::loadSaved()
{
    const QString name = QSettings().value(kThemeSettingsKey).toString();
    if (!name.isEmpty())
        setCurrentByName(name);
}

void ThemeManager::saveChoice(const QString& name)
{
    QSettings().setValue(kThemeSettingsKey, name);
}

QString ThemeManager::dropDownArrowPath(const QColor& colour)
{
    const QString dir = QDir::temp().filePath("OpenDaw");
    QDir().mkpath(dir);
    const QString base = dir + "/arrow-" + colour.name(QColor::HexArgb).mid(1);
    const QString path = base + ".png";
    if (QFile::exists(path)) return path;

    // 1x plus @2x/@3x copies, which Qt picks up on high-DPI screens.
    for (int scale = 1; scale <= 3; ++scale) {
        QPixmap pm(9 * scale, 6 * scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        QPainterPath chevron;
        chevron.moveTo(1.0, 1.5);
        chevron.lineTo(4.5, 4.8);
        chevron.lineTo(8.0, 1.5);
        p.setPen(QPen(colour, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(chevron);
        p.end();
        pm.save(scale == 1 ? path : base + QString("@%1x.png").arg(scale), "PNG");
    }
    return path;
}

const std::vector<QColor>& ThemeManager::trackPalette()
{
    return instance().current().trackColours;
}

} // namespace OpenDaw
