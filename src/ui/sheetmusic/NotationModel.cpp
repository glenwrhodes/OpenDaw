#include "ui/sheetmusic/NotationModel.h"
#include <map>
#include <cmath>
#include <algorithm>

namespace OpenDaw {

// ── helpers ─────────────────────────────────────────────────────────────────

double noteValueBeats(NoteValue v)
{
    switch (v) {
    case NoteValue::Whole:     return 4.0;
    case NoteValue::Half:      return 2.0;
    case NoteValue::Quarter:   return 1.0;
    case NoteValue::Eighth:    return 0.5;
    case NoteValue::Sixteenth: return 0.25;
    }
    return 1.0;
}

static bool isTripletDuration(double beats)
{
    // common triplet durations:
    // eighth triplet: 1/3 beat ≈ 0.333
    // quarter triplet: 2/3 beat ≈ 0.667
    // sixteenth triplet: 1/6 beat ≈ 0.167
    static const double kTripletDurs[] = {
        1.0 / 3.0,   // eighth-note triplet
        2.0 / 3.0,   // quarter-note triplet
        1.0 / 6.0,   // sixteenth-note triplet
        4.0 / 3.0,   // half-note triplet
    };
    for (double td : kTripletDurs)
        if (std::abs(beats - td) < 0.05) return true;
    return false;
}

static NoteValue tripletNoteValue(double beats)
{
    if (std::abs(beats - 1.0 / 6.0) < 0.05) return NoteValue::Sixteenth;
    if (std::abs(beats - 1.0 / 3.0) < 0.05) return NoteValue::Eighth;
    if (std::abs(beats - 2.0 / 3.0) < 0.05) return NoteValue::Quarter;
    if (std::abs(beats - 4.0 / 3.0) < 0.05) return NoteValue::Half;
    return NoteValue::Eighth;
}

NoteValue quantizeDuration(double beats, bool& dotted)
{
    struct Entry { double dur; NoteValue v; bool dot; };
    static const Entry table[] = {
        { 6.0,  NoteValue::Whole,     true  },
        { 4.0,  NoteValue::Whole,     false },
        { 3.0,  NoteValue::Half,      true  },
        { 2.0,  NoteValue::Half,      false },
        { 1.5,  NoteValue::Quarter,   true  },
        { 1.0,  NoteValue::Quarter,   false },
        { 0.75, NoteValue::Eighth,    true  },
        { 0.5,  NoteValue::Eighth,    false },
        { 0.375,NoteValue::Sixteenth, true  },
        { 0.25, NoteValue::Sixteenth, false },
    };

    const Entry* best = &table[9];
    double bestDist = 1e9;
    for (auto& e : table) {
        double d = std::abs(beats - e.dur);
        if (d < bestDist) {
            bestDist = d;
            best = &e;
        }
    }
    dotted = best->dot;
    return best->v;
}

static const int kDiatonicBase[7] = {0, 2, 4, 5, 7, 9, 11}; // C D E F G A B
static const int kSharpOrder[7]  = {3, 0, 4, 1, 5, 2, 6};  // F C G D A E B
static const int kFlatOrder[7]   = {6, 2, 5, 1, 4, 0, 3};  // B E A D G C F

static const int kDiatonicSharp[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
static const int kDiatonicFlat[12]  = {0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6};
static const bool kIsBlackKey[12] = {
    false, true, false, true, false, false, true, false, true, false, true, false
};

NoteSpelling spellMidiNote(int midiNote, int keySig, bool preferFlats)
{
    int pc = midiNote % 12;

    int keyAlts[7] = {};
    if (keySig > 0)
        for (int i = 0; i < std::min(keySig, 7); i++) keyAlts[kSharpOrder[i]] = +1;
    else if (keySig < 0)
        for (int i = 0; i < std::min(-keySig, 7); i++) keyAlts[kFlatOrder[i]] = -1;

    int keyPitch[7];
    for (int d = 0; d < 7; d++)
        keyPitch[d] = (kDiatonicBase[d] + keyAlts[d] + 12) % 12;

    for (int d = 0; d < 7; d++)
        if (keyPitch[d] == pc)
            return {d, Accidental::None};

    // for accidentals outside the key, decide sharp vs flat:
    // flat keys (keySig < 0) → prefer flats
    // sharp keys (keySig > 0) → prefer sharps
    // C major (keySig == 0) → use the preferFlats toggle
    bool useFlats = (keySig < 0) || (keySig == 0 && preferFlats);

    int diatonic;
    if (kIsBlackKey[pc])
        diatonic = useFlats ? kDiatonicFlat[pc] : kDiatonicSharp[pc];
    else
        diatonic = kDiatonicSharp[pc];

    int natChroma = kDiatonicBase[diatonic];

    if (pc == natChroma && keyAlts[diatonic] != 0)
        return {diatonic, Accidental::Natural};
    if (pc == (natChroma + 1) % 12)
        return {diatonic, Accidental::Sharp};
    if (pc == (natChroma + 11) % 12)
        return {diatonic, Accidental::Flat};

    return {diatonic, useFlats ? Accidental::Flat : Accidental::Sharp};
}

int spelledNoteToStaffPosition(int midiNote, int diatonic, StaffKind staff)
{
    int octave = (midiNote / 12) - 1;
    int chroma = midiNote % 12;

    if (diatonic == 6 && chroma < 2)  octave -= 1;   // B# → previous octave
    if (diatonic == 0 && chroma > 10) octave += 1;   // Cb → next octave

    int absDiatonic = octave * 7 + diatonic;

    if (staff == StaffKind::Treble) {
        int e4Abs = 4 * 7 + 2;
        return absDiatonic - e4Abs;
    } else {
        int g2Abs = 2 * 7 + 4;
        return absDiatonic - g2Abs;
    }
}

int stemDirectionForPosition(int staffPosition)
{
    return (staffPosition >= 4) ? -1 : 1;
}

int staffPositionToMidi(int staffPos, StaffKind staff)
{
    return staffPositionToMidi(staffPos, staff, 0);
}

int staffPositionToMidi(int staffPos, StaffKind staff, int keySig)
{
    static const int kChromaFromDiatonic[7] = {0, 2, 4, 5, 7, 9, 11};
    int refAbs = (staff == StaffKind::Treble) ? (4 * 7 + 2) : (2 * 7 + 4);
    int absDiatonic = staffPos + refAbs;
    int octave = absDiatonic / 7;
    int diatonic = absDiatonic % 7;
    if (diatonic < 0) { diatonic += 7; octave--; }

    int basePitch = (octave + 1) * 12 + kChromaFromDiatonic[diatonic];

    // apply key signature alteration to this diatonic degree
    if (keySig > 0) {
        for (int i = 0; i < std::min(keySig, 7); i++)
            if (kSharpOrder[i] == diatonic) { basePitch += 1; break; }
    } else if (keySig < 0) {
        for (int i = 0; i < std::min(-keySig, 7); i++)
            if (kFlatOrder[i] == diatonic) { basePitch -= 1; break; }
    }

    return basePitch;
}

// ── NotationModel ────────────────────────────────────────────────────────────

void NotationModel::clear()
{
    measures_.clear();
    timeSigNum_ = 4;
    timeSigDen_ = 4;
    keySig_ = 0;
}

bool NotationModel::hasStaccato(double beat, int midiNote) const
{
    return hasArticulation(beat, midiNote, ArticulationMarking::Staccato);
}

bool NotationModel::hasArticulation(double beat, int midiNote, ArticulationMarking::Type type) const
{
    for (const auto& a : articulations_)
        if (a.type == type && std::abs(a.beat - beat) < 0.05 && a.midiNote == midiNote)
            return true;
    return false;
}

std::vector<ArticulationMarking::Type> NotationModel::getArticulations(double beat, int midiNote) const
{
    std::vector<ArticulationMarking::Type> result;
    for (const auto& a : articulations_)
        if (std::abs(a.beat - beat) < 0.05 && a.midiNote == midiNote)
            result.push_back(a.type);
    return result;
}

Accidental NotationModel::getSpellingOverride(double beat, int midiNote) const
{
    for (const auto& s : spellingOverrides_)
        if (std::abs(s.beat - beat) < 0.05 && s.midiNote == midiNote)
            return s.forced;
    return Accidental::None;
}

void NotationModel::buildFromClip(te::MidiClip* clip, int timeSigNum, int timeSigDen,
                                  int keySig, bool preferFlats)
{
    clear();
    if (!clip) return;

    timeSigNum_ = timeSigNum;
    timeSigDen_ = timeSigDen;
    keySig_ = std::max(-7, std::min(7, keySig));
    double beatsPerMeasure = timeSigNum * (4.0 / timeSigDen);

    auto& seq = clip->getSequence();
    auto& rawNotes = seq.getNotes();

    // find total extent
    double maxBeat = beatsPerMeasure;
    for (auto* note : rawNotes) {
        double endBeat = note->getStartBeat().inBeats() + note->getLengthBeats().inBeats();
        if (endBeat > maxBeat) maxBeat = endBeat;
    }
    int numMeasures = std::max(1, static_cast<int>(std::ceil(maxBeat / beatsPerMeasure)));

    // pre-allocate measure event buckets
    std::vector<std::vector<NotationEvent>> trebleBuckets(numMeasures);
    std::vector<std::vector<NotationEvent>> bassBuckets(numMeasures);

    // bucket each MIDI note into its measure and staff
    for (auto* note : rawNotes) {
        int midiPitch = note->getNoteNumber();
        double startBeat = note->getStartBeat().inBeats();
        double lenBeats = note->getLengthBeats().inBeats();

        int measIdx = std::min(static_cast<int>(startBeat / beatsPerMeasure), numMeasures - 1);
        double beatInMeas = startBeat - measIdx * beatsPerMeasure;

        double remainInMeasure = beatsPerMeasure - beatInMeas;
        double clampedLen = std::min(lenBeats, remainInMeasure);

        StaffKind staff = (midiPitch >= 60) ? StaffKind::Treble : StaffKind::Bass;

        auto spelling = spellMidiNote(midiPitch, keySig_, preferFlats);

        Accidental overrideAcc = getSpellingOverride(startBeat, midiPitch);
        if (overrideAcc == Accidental::Flat) {
            int aboveChroma = (midiPitch + 1) % 12;
            static const int kDiaFromChroma[12] = {0,0,1,1,2,3,3,4,4,5,5,6};
            spelling.diatonic = kDiaFromChroma[aboveChroma];
            spelling.display = Accidental::Flat;
        } else if (overrideAcc == Accidental::Sharp) {
            int belowChroma = (midiPitch - 1 + 12) % 12;
            static const int kDiaFromChroma[12] = {0,0,1,1,2,3,3,4,4,5,5,6};
            spelling.diatonic = kDiaFromChroma[belowChroma];
            spelling.display = Accidental::Sharp;
        } else if (overrideAcc == Accidental::Natural) {
            static const int kDiaFromChroma[12] = {0,0,1,1,2,3,3,4,4,5,5,6};
            spelling.diatonic = kDiaFromChroma[midiPitch % 12];
            spelling.display = Accidental::Natural;
        }

        NotationNote nn;
        nn.midiNote = midiPitch;
        nn.staff = staff;
        nn.staffPosition = spelledNoteToStaffPosition(midiPitch, spelling.diatonic, staff);
        nn.accidental = spelling.display;
        nn.stemDirection = stemDirectionForPosition(nn.staffPosition);
        nn.beatInMeasure = beatInMeas;
        bool triplet = isTripletDuration(clampedLen);
        if (triplet) {
            nn.value = tripletNoteValue(clampedLen);
            nn.dotted = false;
        } else {
            nn.value = quantizeDuration(clampedLen, nn.dotted);
        }
        nn.engineNote = note;

        // find or create an event at this beat (for chord grouping)
        auto& bucket = (staff == StaffKind::Treble) ? trebleBuckets[measIdx]
                                                     : bassBuckets[measIdx];
        constexpr double chordEps = 0.05;
        bool merged = false;
        for (auto& evt : bucket) {
            if (!evt.isRest && std::abs(evt.beatInMeasure - beatInMeas) < chordEps) {
                evt.notes.push_back(nn);
                merged = true;
                break;
            }
        }
        if (!merged) {
            NotationEvent evt;
            evt.isRest = false;
            evt.isTriplet = triplet;
            evt.beatInMeasure = beatInMeas;
            evt.value = nn.value;
            evt.dotted = nn.dotted;
            evt.staff = staff;
            evt.notes.push_back(nn);
            bucket.push_back(evt);
        }
    }

    // build measures
    measures_.resize(numMeasures);
    for (int m = 0; m < numMeasures; ++m) {
        measures_[m].measureNumber = m;

        insertRests(trebleBuckets[m], StaffKind::Treble, beatsPerMeasure);
        insertRests(bassBuckets[m], StaffKind::Bass, beatsPerMeasure);

        measures_[m].trebleEvents = std::move(trebleBuckets[m]);
        measures_[m].bassEvents = std::move(bassBuckets[m]);

        buildBeamGroups(measures_[m].trebleEvents, StaffKind::Treble, measures_[m].trebleBeams);
        buildBeamGroups(measures_[m].bassEvents, StaffKind::Bass, measures_[m].bassBeams);

        for (auto& bg : measures_[m].trebleBeams)
            for (int idx : bg.eventIndices)
                measures_[m].trebleEvents[idx].beamed = true;
        for (auto& bg : measures_[m].bassBeams)
            for (int idx : bg.eventIndices)
                measures_[m].bassEvents[idx].beamed = true;

        detectTriplets(measures_[m].trebleEvents, StaffKind::Treble, measures_[m].trebleTriplets);
        detectTriplets(measures_[m].bassEvents, StaffKind::Bass, measures_[m].bassTriplets);
    }
}

// ── rest insertion ──────────────────────────────────────────────────────────

static void fillGapWithRests(std::vector<NotationEvent>& out, double from, double to, StaffKind staff)
{
    constexpr double eps = 0.01;
    double cursor = from;
    while (to - cursor > eps) {
        double gap = to - cursor;
        bool dot = false;
        NoteValue rv = quantizeDuration(gap, dot);
        double rvBeats = noteValueBeats(rv) * (dot ? 1.5 : 1.0);
        if (rvBeats > gap + eps) {
            if (gap >= 2.0)      { rv = NoteValue::Half;      dot = false; }
            else if (gap >= 1.0) { rv = NoteValue::Quarter;   dot = false; }
            else if (gap >= 0.5) { rv = NoteValue::Eighth;    dot = false; }
            else                 { rv = NoteValue::Sixteenth;  dot = false; }
            rvBeats = noteValueBeats(rv) * (dot ? 1.5 : 1.0);
        }

        NotationEvent restEvt;
        restEvt.isRest = true;
        restEvt.beatInMeasure = cursor;
        restEvt.value = rv;
        restEvt.dotted = dot;
        restEvt.staff = staff;
        restEvt.rest.value = rv;
        restEvt.rest.dotted = dot;
        restEvt.rest.beatInMeasure = cursor;
        restEvt.rest.staff = staff;
        out.push_back(restEvt);

        cursor += rvBeats;
    }
}

void NotationModel::insertRests(std::vector<NotationEvent>& events,
                                StaffKind staff, double beatsPerMeasure)
{
    std::sort(events.begin(), events.end(),
              [](const NotationEvent& a, const NotationEvent& b) {
                  return a.beatInMeasure < b.beatInMeasure;
              });

    std::vector<NotationEvent> result;
    double cursor = 0.0;
    constexpr double eps = 0.01;

    for (auto& evt : events) {
        if (evt.beatInMeasure - cursor > eps)
            fillGapWithRests(result, cursor, evt.beatInMeasure, staff);

        result.push_back(evt);
        double evtDur;
        if (evt.isTriplet) {
            // triplet durations: eighth=1/3, quarter=2/3, sixteenth=1/6, half=4/3
            switch (evt.value) {
            case NoteValue::Sixteenth: evtDur = 1.0 / 6.0; break;
            case NoteValue::Eighth:    evtDur = 1.0 / 3.0; break;
            case NoteValue::Quarter:   evtDur = 2.0 / 3.0; break;
            case NoteValue::Half:      evtDur = 4.0 / 3.0; break;
            default:                   evtDur = 1.0 / 3.0; break;
            }
        } else {
            evtDur = noteValueBeats(evt.value) * (evt.dotted ? 1.5 : 1.0);
        }
        cursor = evt.beatInMeasure + evtDur;
    }

    if (beatsPerMeasure - cursor > eps)
        fillGapWithRests(result, cursor, beatsPerMeasure, staff);

    events = std::move(result);
}

// ── beam grouping ──────────────────────────────────────────────────────────

void NotationModel::buildBeamGroups(const std::vector<NotationEvent>& events,
                                    StaffKind staff,
                                    std::vector<BeamGroup>& beamsOut)
{
    beamsOut.clear();

    // Half-bar beam grouping for simple meters:
    // 4/4 → group beats [0,2) and [2,4)  (beam 4 eighths together per half)
    // 2/4 → group beats [0,2)            (beam full bar)
    // 3/4 → group beats [0,1), [1,2), [2,3)  (beam per beat -- triple meter)
    // 6/8 → group beats [0,3), [3,6)
    double beatsPerMeasure = timeSigNum_ * (4.0 / timeSigDen_);
    double groupSize = 1.0;  // fallback: per-beat
    if (timeSigDen_ <= 4) {
        if (timeSigNum_ == 4)      groupSize = 2.0;
        else if (timeSigNum_ == 2) groupSize = 2.0;
        else                       groupSize = 1.0;
    } else {
        groupSize = 3.0 * (4.0 / timeSigDen_);
    }

    auto beamGroupOf = [&](double beat) -> int {
        return static_cast<int>(beat / groupSize);
    };

    BeamGroup current;
    current.staff = staff;

    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        auto& evt = events[i];
        if (evt.isRest) {
            if (current.eventIndices.size() >= 2)
                beamsOut.push_back(current);
            current.eventIndices.clear();
            continue;
        }

        bool beamable = (evt.value == NoteValue::Eighth || evt.value == NoteValue::Sixteenth);
        if (!beamable) {
            if (current.eventIndices.size() >= 2)
                beamsOut.push_back(current);
            current.eventIndices.clear();
            continue;
        }

        if (!current.eventIndices.empty()) {
            auto& prevEvt = events[current.eventIndices.back()];
            if (beamGroupOf(prevEvt.beatInMeasure) != beamGroupOf(evt.beatInMeasure)) {
                if (current.eventIndices.size() >= 2)
                    beamsOut.push_back(current);
                current.eventIndices.clear();
            }
        }

        current.eventIndices.push_back(i);
    }

    if (current.eventIndices.size() >= 2)
        beamsOut.push_back(current);

    (void)beatsPerMeasure;
}

// ── triplet detection ───────────────────────────────────────────────────────

void NotationModel::detectTriplets(std::vector<NotationEvent>& events,
                                    StaffKind staff,
                                    std::vector<TripletGroup>& tripletsOut)
{
    tripletsOut.clear();

    // scan for consecutive triplet-flagged events that form groups of 3
    int i = 0;
    int n = static_cast<int>(events.size());
    while (i < n) {
        if (!events[i].isRest && events[i].isTriplet) {
            // try to collect 3 consecutive triplet events
            int start = i;
            int count = 0;
            while (i < n && !events[i].isRest && events[i].isTriplet && count < 3) {
                count++;
                i++;
            }
            if (count == 3) {
                TripletGroup tg;
                tg.staff = staff;
                for (int j = start; j < start + 3; j++)
                    tg.eventIndices.push_back(j);
                tripletsOut.push_back(tg);

                // also beam these together if not already beamed
                for (int j = start; j < start + 3; j++)
                    events[j].beamed = true;
            }
        } else {
            i++;
        }
    }
}

} // namespace OpenDaw
