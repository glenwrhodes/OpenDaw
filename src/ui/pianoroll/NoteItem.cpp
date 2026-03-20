#include "NoteItem.h"
#include "NoteGrid.h"
#include "ChannelColors.h"
#include "ui/timeline/GridSnapper.h"
#include "utils/ThemeManager.h"
#include <QPainter>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsSceneMouseEvent>
#include <QCursor>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <algorithm>

namespace freedaw {

NoteItem::NoteItem(te::MidiNote* note, te::MidiClip* clip,
                   double pixelsPerBeat, double noteRowHeight,
                   int lowestNote, int channelNumber,
                   QGraphicsItem* parent)
    : QGraphicsRectItem(parent), note_(note), clip_(clip),
      channelNumber_(channelNumber),
      pixelsPerBeat_(pixelsPerBeat), noteRowHeight_(noteRowHeight),
      lowestNote_(lowestNote)
{
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(QCursor(Qt::PointingHandCursor));
    setAcceptedMouseButtons(Qt::LeftButton);
}

void NoteItem::updateGeometry(double pixelsPerBeat, double noteRowHeight,
                              int lowestNote, int totalNotes,
                              double beatOffset)
{
    pixelsPerBeat_ = pixelsPerBeat;
    noteRowHeight_ = noteRowHeight;
    lowestNote_ = lowestNote;
    beatOffset_ = beatOffset;

    double startBeat = note_->getStartBeat().inBeats() + beatOffset;
    double lengthBeats = note_->getLengthBeats().inBeats();
    int pitch = note_->getNoteNumber();

    double x = startBeat * pixelsPerBeat;
    double w = std::max(2.0, lengthBeats * pixelsPerBeat);
    int row = (lowestNote + totalNotes - 1) - pitch;
    double y = row * noteRowHeight;
    double h = noteRowHeight - 1.0;

    setRect(0, 0, w, h);
    setPos(x, y);
}

void NoteItem::paint(QPainter* painter,
                     const QStyleOptionGraphicsItem*,
                     QWidget*)
{
    auto& theme = ThemeManager::instance().current();
    QRectF r = rect();

    QColor baseColor = channelColor(channelNumber_);
    QColor color;
    if (isSelected()) {
        QColor sel = theme.pianoRollNoteSelected;
        color = QColor((sel.red() + baseColor.red()) / 2,
                       (sel.green() + baseColor.green()) / 2,
                       (sel.blue() + baseColor.blue()) / 2);
    } else {
        color = baseColor;
    }

    int velocity = note_->getVelocity();
    double alpha = 0.6 + 0.4 * (velocity / 127.0);
    if (!isActiveChannel_)
        alpha *= 0.35;
    color.setAlphaF(alpha);

    painter->fillRect(r, color);

    if (velocity == 0) {
        QPen stripePen(QColor(200, 60, 60, 160), 1.0);
        painter->setPen(stripePen);
        painter->drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        painter->drawLine(r.topLeft(), r.bottomRight());
    } else {
        painter->setPen(QPen(color.darker(130), 0.5));
        painter->drawRect(r);
    }
}

void NoteItem::collectPeers()
{
    peers_.clear();
    if (!scene()) return;
    for (auto* item : scene()->selectedItems()) {
        auto* peer = dynamic_cast<NoteItem*>(item);
        if (peer && peer != this) {
            peers_.push_back({
                peer,
                peer->note_->getStartBeat().inBeats(),
                peer->note_->getLengthBeats().inBeats(),
                peer->note_->getNoteNumber()
            });
        }
    }
}

void NoteItem::createGhosts()
{
    if (!scene()) return;
    auto& theme = ThemeManager::instance().current();
    QColor ghostColor = theme.pianoRollNote;
    ghostColor.setAlpha(80);
    QPen ghostPen(theme.pianoRollNote.lighter(150), 1.0, Qt::DashLine);
    constexpr int totalNotes = 128;

    auto makeGhost = [&](double beat, double length, int pitch) {
        double x = beat * pixelsPerBeat_;
        int row = (lowestNote_ + totalNotes - 1) - pitch;
        double y = row * noteRowHeight_;
        double w = std::max(2.0, length * pixelsPerBeat_);
        double h = noteRowHeight_ - 1.0;
        auto* g = scene()->addRect(x, y, w, h, ghostPen, QBrush(ghostColor));
        g->setZValue(1.5);
        ghostItems_.push_back(g);
    };

    makeGhost(origStartBeat_, origLengthBeats_, origPitch_);
    for (auto& p : peers_)
        makeGhost(p.origBeat, p.origLength, p.origPitch);
}

void NoteItem::destroyGhosts()
{
    for (auto* g : ghostItems_) {
        if (scene()) scene()->removeItem(g);
        delete g;
    }
    ghostItems_.clear();
}

void NoteItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsRectItem::mousePressEvent(event);
        return;
    }

    const bool additiveSelect = event->modifiers() & Qt::ShiftModifier;
    if (!additiveSelect && !isSelected() && scene())
        scene()->clearSelection();
    setSelected(true);
    dragStartScene_ = event->scenePos();
    origStartBeat_ = note_->getStartBeat().inBeats();
    origLengthBeats_ = note_->getLengthBeats().inBeats();
    origPitch_ = note_->getNoteNumber();

    if (clip_) {
        if (auto* track = clip_->getAudioTrack()) {
            int vel = std::max(1, note_->getVelocity());
            auto msg = juce::MidiMessage::noteOn(1, origPitch_, static_cast<juce::uint8>(vel));
            track->injectLiveMidiMessage(msg, 0);
            previewingNote_ = origPitch_;
        }
    }

    double localX = event->pos().x();
    constexpr double edgeGrab = 6.0;

    if (localX >= rect().width() - edgeGrab) {
        resizingRight_ = true;
        setCursor(QCursor(Qt::SizeHorCursor));
    } else if (localX <= edgeGrab) {
        resizingLeft_ = true;
        setCursor(QCursor(Qt::SizeHorCursor));
    } else {
        dragging_ = true;
        setCursor(QCursor(Qt::ClosedHandCursor));
    }

    collectPeers();
    event->accept();
}

void NoteItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    QPointF delta = event->scenePos() - dragStartScene_;
    constexpr int totalNotes = 128;

    if (resizingRight_) {
        double newEndBeat = origStartBeat_ + origLengthBeats_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newEndBeat = snapper_->snapBeat(newEndBeat);
        double newLength = std::max(0.125, newEndBeat - origStartBeat_);
        double lengthDelta = newLength - origLengthBeats_;
        double w = std::max(2.0, newLength * pixelsPerBeat_);
        setRect(0, 0, w, rect().height());

        for (auto& p : peers_) {
            double pLen = std::max(0.125, p.origLength + lengthDelta);
            double pw = std::max(2.0, pLen * pixelsPerBeat_);
            p.item->setRect(0, 0, pw, p.item->rect().height());
        }
    } else if (resizingLeft_) {
        double newStartBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newStartBeat = snapper_->snapBeat(newStartBeat);
        if (newStartBeat < 0) newStartBeat = 0;
        double newLength = (origStartBeat_ + origLengthBeats_) - newStartBeat;
        if (newLength < 0.125) return;
        double x = newStartBeat * pixelsPerBeat_;
        double w = std::max(2.0, newLength * pixelsPerBeat_);
        setPos(x, pos().y());
        setRect(0, 0, w, rect().height());

        double beatShift = newStartBeat - origStartBeat_;
        for (auto& p : peers_) {
            double pStart = p.origBeat + beatShift;
            if (pStart < 0) pStart = 0;
            double pLen = (p.origBeat + p.origLength) - pStart;
            if (pLen < 0.125) continue;
            double px = pStart * pixelsPerBeat_;
            double pw = std::max(2.0, pLen * pixelsPerBeat_);
            p.item->setPos(px, p.item->pos().y());
            p.item->setRect(0, 0, pw, p.item->rect().height());
        }
    } else if (dragging_) {
        double newBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newBeat = snapper_->snapBeat(newBeat);
        if (newBeat < 0) newBeat = 0;
        double beatDelta = newBeat - origStartBeat_;

        int pitchDelta = -static_cast<int>(std::round(delta.y() / noteRowHeight_));
        int newPitch = std::clamp(origPitch_ + pitchDelta, 0, 127);
        int row = (lowestNote_ + totalNotes - 1) - newPitch;
        setPos(newBeat * pixelsPerBeat_, row * noteRowHeight_);

        for (auto& p : peers_) {
            double pBeat = std::max(0.0, p.origBeat + beatDelta);
            int pPitch = std::clamp(p.origPitch + pitchDelta, 0, 127);
            int pRow = (lowestNote_ + totalNotes - 1) - pPitch;
            p.item->setPos(pBeat * pixelsPerBeat_, pRow * noteRowHeight_);
        }

        bool ctrlHeld = (event->modifiers() & Qt::ControlModifier);
        setCursor(QCursor(ctrlHeld ? Qt::DragCopyCursor : Qt::ClosedHandCursor));
        if (ctrlHeld && ghostItems_.empty())
            createGhosts();
        else if (!ctrlHeld && !ghostItems_.empty())
            destroyGhosts();
    }
    event->accept();
}

void NoteItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QGraphicsRectItem::mouseReleaseEvent(event);
        return;
    }

    setCursor(QCursor(Qt::PointingHandCursor));
    QPointF delta = event->scenePos() - dragStartScene_;

    auto* um = clip_ ? &clip_->edit.getUndoManager() : nullptr;

    if (resizingRight_) {
        double newEndBeat = origStartBeat_ + origLengthBeats_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newEndBeat = snapper_->snapBeat(newEndBeat);
        double newLength = std::max(0.125, newEndBeat - origStartBeat_);
        double lengthDelta = newLength - origLengthBeats_;
        note_->setStartAndLength(
            tracktion::BeatPosition::fromBeats(origStartBeat_),
            tracktion::BeatDuration::fromBeats(newLength), um);
        for (auto& p : peers_) {
            double pLen = std::max(0.125, p.origLength + lengthDelta);
            p.item->note_->setStartAndLength(
                tracktion::BeatPosition::fromBeats(p.origBeat),
                tracktion::BeatDuration::fromBeats(pLen), um);
        }
    } else if (resizingLeft_) {
        double newStartBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newStartBeat = snapper_->snapBeat(newStartBeat);
        if (newStartBeat < 0) newStartBeat = 0;
        double newLength = (origStartBeat_ + origLengthBeats_) - newStartBeat;
        if (newLength >= 0.125) {
            note_->setStartAndLength(
                tracktion::BeatPosition::fromBeats(newStartBeat),
                tracktion::BeatDuration::fromBeats(newLength), um);
            double beatShift = newStartBeat - origStartBeat_;
            for (auto& p : peers_) {
                double pStart = p.origBeat + beatShift;
                if (pStart < 0) pStart = 0;
                double pLen = (p.origBeat + p.origLength) - pStart;
                if (pLen >= 0.125) {
                    p.item->note_->setStartAndLength(
                        tracktion::BeatPosition::fromBeats(pStart),
                        tracktion::BeatDuration::fromBeats(pLen), um);
                }
            }
        }
    } else if (dragging_) {
        double newBeat = origStartBeat_ + delta.x() / pixelsPerBeat_;
        if (snapper_) newBeat = snapper_->snapBeat(newBeat);
        if (newBeat < 0) newBeat = 0;
        double beatDelta = newBeat - origStartBeat_;
        int pitchDelta = -static_cast<int>(std::round(delta.y() / noteRowHeight_));
        int newPitch = std::clamp(origPitch_ + pitchDelta, 0, 127);

        bool copyDrag = (event->modifiers() & Qt::ControlModifier);

        if (copyDrag && clip_) {
            auto& seq = clip_->getSequence();
            um->beginNewTransaction("Copy-Drag Notes");

            struct CopyInfo { double beat; int pitch; double length; int vel; };
            std::vector<CopyInfo> copies;
            copies.push_back({ newBeat, newPitch, origLengthBeats_, note_->getVelocity() });
            for (auto& p : peers_) {
                double pBeat = std::max(0.0, p.origBeat + beatDelta);
                int pPitch = std::clamp(p.origPitch + pitchDelta, 0, 127);
                copies.push_back({ pBeat, pPitch, p.origLength, p.item->note_->getVelocity() });
            }
            for (auto& c : copies) {
                seq.addNote(c.pitch,
                            tracktion::BeatPosition::fromBeats(c.beat),
                            tracktion::BeatDuration::fromBeats(c.length),
                            c.vel, 0, um);
            }

            auto* grid = qobject_cast<NoteGrid*>(
                scene() && !scene()->views().isEmpty() ? scene()->views().first() : nullptr);
            if (grid) {
                std::vector<ClipboardNote> pending;
                for (auto& c : copies)
                    pending.push_back({ c.pitch, c.beat, c.length, c.vel });
                grid->setPendingCopySelect(std::move(pending));
            }
        } else {
            note_->setStartAndLength(
                tracktion::BeatPosition::fromBeats(newBeat),
                tracktion::BeatDuration::fromBeats(origLengthBeats_), um);
            note_->setNoteNumber(newPitch, um);
            for (auto& p : peers_) {
                double pBeat = std::max(0.0, p.origBeat + beatDelta);
                int pPitch = std::clamp(p.origPitch + pitchDelta, 0, 127);
                p.item->note_->setStartAndLength(
                    tracktion::BeatPosition::fromBeats(pBeat),
                    tracktion::BeatDuration::fromBeats(p.origLength), um);
                p.item->note_->setNoteNumber(pPitch, um);
            }
        }
    }

    if (previewingNote_ >= 0 && clip_) {
        if (auto* track = clip_->getAudioTrack()) {
            auto msg = juce::MidiMessage::noteOff(1, previewingNote_);
            track->injectLiveMidiMessage(msg, 0);
        }
        previewingNote_ = -1;
    }

    destroyGhosts();
    dragging_ = false;
    resizingRight_ = false;
    resizingLeft_ = false;
    peers_.clear();

    if (refreshCb_) refreshCb_();
    event->accept();
}

void NoteItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        event->accept();
        showEditDialog();
        return;
    }
    QGraphicsRectItem::mouseDoubleClickEvent(event);
}

QString NoteItem::pitchToString(int midiNote)
{
    static const char* kNames[12] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    midiNote = std::clamp(midiNote, 0, 127);
    return QString("%1%2").arg(kNames[midiNote % 12]).arg(midiNote / 12 - 1);
}

int NoteItem::parsePitchString(const QString& text, bool* ok)
{
    QString trimmed = text.trimmed();

    bool numOk = false;
    int num = trimmed.toInt(&numOk);
    if (numOk && num >= 0 && num <= 127) {
        if (ok) *ok = true;
        return num;
    }

    static const QRegularExpression re(
        R"(^([A-Ga-g])(#{1,2}|b{1,2})?(-?\d)$)");
    auto match = re.match(trimmed);
    if (!match.hasMatch()) {
        if (ok) *ok = false;
        return -1;
    }

    static const int baseMap[7] = { 0, 2, 4, 5, 7, 9, 11 };
    QChar letter = match.captured(1).toUpper()[0];
    int letterIndex = letter.unicode() - 'C';
    if (letterIndex < 0) letterIndex += 7;

    int semitone = baseMap[letterIndex];
    QString accidental = match.captured(2);
    if (accidental == "#")        semitone += 1;
    else if (accidental == "##")  semitone += 2;
    else if (accidental == "b")   semitone -= 1;
    else if (accidental == "bb")  semitone -= 2;

    int octave = match.captured(3).toInt();
    int midi = (octave + 1) * 12 + semitone;
    if (midi < 0 || midi > 127) {
        if (ok) *ok = false;
        return -1;
    }
    if (ok) *ok = true;
    return midi;
}

void NoteItem::showEditDialog()
{
    QWidget* parentWidget = nullptr;
    if (scene() && !scene()->views().isEmpty())
        parentWidget = scene()->views().first();

    auto* dlg = new QDialog(parentWidget);
    dlg->setWindowTitle("Edit Note");
    dlg->setAccessibleName("Edit Note Dialog");
    dlg->setFixedWidth(260);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* form = new QFormLayout();
    form->setContentsMargins(12, 12, 12, 6);

    auto* pitchEdit = new QLineEdit(dlg);
    pitchEdit->setAccessibleName("Note Pitch");
    pitchEdit->setText(pitchToString(note_->getNoteNumber()));
    pitchEdit->setToolTip("Enter note name (e.g. C4, F#3) or MIDI number (0-127)");
    pitchEdit->selectAll();
    form->addRow("Pitch:", pitchEdit);

    auto* velSpin = new QSpinBox(dlg);
    velSpin->setAccessibleName("Note Velocity");
    velSpin->setRange(1, 127);
    velSpin->setValue(note_->getVelocity());
    form->addRow("Velocity:", velSpin);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* okBtn = new QPushButton("OK", dlg);
    okBtn->setAccessibleName("OK");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton("Cancel", dlg);
    cancelBtn->setAccessibleName("Cancel");
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(dlg);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    auto* notePtr = note_;
    auto* clipPtr = clip_;
    auto refreshCb = refreshCb_;

    QObject::connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    QObject::connect(okBtn, &QPushButton::clicked, dlg, [=]() {
        bool pitchOk = false;
        int newPitch = parsePitchString(pitchEdit->text(), &pitchOk);
        if (!pitchOk) {
            pitchEdit->setStyleSheet("border: 1px solid red;");
            return;
        }
        int newVel = velSpin->value();
        auto* um = clipPtr ? &clipPtr->edit.getUndoManager() : nullptr;
        notePtr->setNoteNumber(newPitch, um);
        notePtr->setVelocity(newVel, um);
        dlg->accept();
        if (refreshCb) refreshCb();
    });

    dlg->exec();
}

} // namespace freedaw
