#pragma once

#include <vector>
#include <cmath>
#include <algorithm>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion::engine;

namespace OpenDaw {

enum class NoteValue { Whole, Half, Quarter, Eighth, Sixteenth };
enum class Accidental { None, Sharp, Flat, Natural };
enum class StaffKind { Treble, Bass };

struct NoteSpelling {
    int diatonic = 0;
    Accidental display = Accidental::None;
};

struct NotationNote {
    int midiNote = 60;
    NoteValue value = NoteValue::Quarter;
    bool dotted = false;
    double beatInMeasure = 0.0;
    int staffPosition = 0;
    Accidental accidental = Accidental::None;
    StaffKind staff = StaffKind::Treble;
    int stemDirection = -1;
    te::MidiNote* engineNote = nullptr;
};

struct NotationRest {
    NoteValue value = NoteValue::Quarter;
    bool dotted = false;
    double beatInMeasure = 0.0;
    StaffKind staff = StaffKind::Treble;
};

struct NotationEvent {
    bool isRest = false;
    bool beamed = false;
    bool isTriplet = false;
    std::vector<NotationNote> notes;
    NotationRest rest;
    double beatInMeasure = 0.0;
    NoteValue value = NoteValue::Quarter;
    bool dotted = false;
    StaffKind staff = StaffKind::Treble;
};

struct BeamGroup {
    std::vector<int> eventIndices;
    StaffKind staff = StaffKind::Treble;
};

struct TripletGroup {
    std::vector<int> eventIndices;
    StaffKind staff = StaffKind::Treble;
};

struct NotationMeasure {
    int measureNumber = 0;
    std::vector<NotationEvent> trebleEvents;
    std::vector<NotationEvent> bassEvents;
    std::vector<BeamGroup> trebleBeams;
    std::vector<BeamGroup> bassBeams;
    std::vector<TripletGroup> trebleTriplets;
    std::vector<TripletGroup> bassTriplets;
};

struct PhraseMarking {
    double startBeat = 0.0;
    double endBeat = 0.0;
    StaffKind staff = StaffKind::Treble;
};

struct ArticulationMarking {
    double beat = 0.0;
    int midiNote = 60;
    StaffKind staff = StaffKind::Treble;
    enum Type { Staccato, Tenuto, Marcato, Accent, Fermata, Staccatissimo } type = Staccato;
};

struct SpellingOverride {
    double beat = 0.0;
    int midiNote = 60;
    Accidental forced = Accidental::None;
};

double noteValueBeats(NoteValue v);
NoteValue quantizeDuration(double beats, bool& dotted);
NoteSpelling spellMidiNote(int midiNote, int keySig, bool preferFlats = false);
int spelledNoteToStaffPosition(int midiNote, int diatonic, StaffKind staff);
int stemDirectionForPosition(int staffPosition);
int staffPositionToMidi(int staffPos, StaffKind staff);
int staffPositionToMidi(int staffPos, StaffKind staff, int keySig);

class NotationModel {
public:
    void buildFromClip(te::MidiClip* clip, int timeSigNum, int timeSigDen,
                       int keySig = 0, bool preferFlats = false);
    void clear();

    const std::vector<NotationMeasure>& measures() const { return measures_; }
    int timeSigNum() const { return timeSigNum_; }
    int timeSigDen() const { return timeSigDen_; }
    int keySig() const { return keySig_; }
    int measureCount() const { return static_cast<int>(measures_.size()); }

    void setPhrases(const std::vector<PhraseMarking>& p) { phrases_ = p; }
    void clearPhrases() { phrases_.clear(); }
    const std::vector<PhraseMarking>& phrases() const { return phrases_; }

    void setArticulations(const std::vector<ArticulationMarking>& a) { articulations_ = a; }
    void clearArticulations() { articulations_.clear(); }
    const std::vector<ArticulationMarking>& articulations() const { return articulations_; }
    bool hasStaccato(double beat, int midiNote) const;
    bool hasArticulation(double beat, int midiNote, ArticulationMarking::Type type) const;
    std::vector<ArticulationMarking::Type> getArticulations(double beat, int midiNote) const;

    void setSpellingOverrides(const std::vector<SpellingOverride>& s) { spellingOverrides_ = s; }
    const std::vector<SpellingOverride>& spellingOverrides() const { return spellingOverrides_; }
    Accidental getSpellingOverride(double beat, int midiNote) const;

private:
    void insertRests(std::vector<NotationEvent>& events,
                     StaffKind staff, double beatsPerMeasure);
    void buildBeamGroups(const std::vector<NotationEvent>& events,
                         StaffKind staff,
                         std::vector<BeamGroup>& beamsOut);
    void detectTriplets(std::vector<NotationEvent>& events,
                        StaffKind staff,
                        std::vector<TripletGroup>& tripletsOut);

    std::vector<NotationMeasure> measures_;
    std::vector<PhraseMarking> phrases_;
    std::vector<ArticulationMarking> articulations_;
    std::vector<SpellingOverride> spellingOverrides_;
    int timeSigNum_ = 4;
    int timeSigDen_ = 4;
    int keySig_ = 0;
};

} // namespace OpenDaw
