#include "AiToolDefs.h"
#include <QJsonObject>

namespace freedaw {

static QJsonObject makeTool(const QString& name, const QString& description,
                            const QJsonObject& properties,
                            const QJsonArray& required = {})
{
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = properties;
    if (!required.isEmpty())
        schema["required"] = required;

    QJsonObject inputSchema;
    inputSchema["type"] = "object";
    inputSchema["properties"] = properties;
    if (!required.isEmpty())
        inputSchema["required"] = required;

    QJsonObject tool;
    tool["name"] = name;
    tool["description"] = description;
    tool["input_schema"] = inputSchema;
    return tool;
}

static QJsonObject prop(const QString& type, const QString& description)
{
    QJsonObject p;
    p["type"] = type;
    p["description"] = description;
    return p;
}

static QJsonObject propNumber(const QString& description, double min, double max)
{
    QJsonObject p;
    p["type"] = "number";
    p["description"] = description;
    p["minimum"] = min;
    p["maximum"] = max;
    return p;
}

static QJsonObject trackProp()
{
    QJsonObject p;
    p["description"] = "Track name (string) or zero-based index (integer).";
    QJsonArray oneOf;
    QJsonObject strType; strType["type"] = "string";
    QJsonObject intType; intType["type"] = "integer";
    oneOf.append(strType);
    oneOf.append(intType);
    p["oneOf"] = oneOf;
    return p;
}

QJsonArray AiToolDefs::allTools()
{
    QJsonArray tools;

    // ── Track Management ────────────────────────────────────────────────────

    tools.append(makeTool("get_project_info",
        "Get an overview of the current project: track count, tempo, time signature, transport state, and a list of all tracks with basic info.",
        {}));

    tools.append(makeTool("get_track_list",
        "Get a list of all tracks with their name, index, type (audio/midi/bus), mute, solo, volume (dB), and pan.",
        {}));

    {
        QJsonObject props;
        props["track"] = trackProp();
        tools.append(makeTool("get_track_info",
            "Get detailed information about a specific track: name, type, mute, solo, volume, pan, input, output, effects list, armed state, mono state.",
            props, {"track"}));
    }

    {
        QJsonObject props;
        props["name"] = prop("string", "Optional name for the new track.");
        tools.append(makeTool("create_audio_track",
            "Create a new audio track. Optionally set its name.",
            props));
    }

    {
        QJsonObject props;
        props["name"] = prop("string", "Optional name for the new MIDI track.");
        tools.append(makeTool("create_midi_track",
            "Create a new MIDI track. Optionally set its name.",
            props));
    }

    {
        QJsonObject props;
        props["name"] = prop("string", "Optional name for the new bus track.");
        tools.append(makeTool("create_bus_track",
            "Create a new bus track for submixing. Optionally set its name.",
            props));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        tools.append(makeTool("delete_track",
            "Delete a track by name or index. This is a destructive action.",
            props, {"track"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["new_name"] = prop("string", "The new name for the track.");
        tools.append(makeTool("rename_track",
            "Rename a track.",
            props, {"track", "new_name"}));
    }

    // ── Track Properties ────────────────────────────────────────────────────

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["muted"] = prop("boolean", "True to mute, false to unmute.");
        tools.append(makeTool("set_track_mute",
            "Mute or unmute a track.",
            props, {"track", "muted"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["solo"] = prop("boolean", "True to solo, false to unsolo.");
        tools.append(makeTool("set_track_solo",
            "Solo or unsolo a track.",
            props, {"track", "solo"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["volume_db"] = propNumber("Volume in decibels.", -60.0, 6.0);
        tools.append(makeTool("set_track_volume",
            "Set the volume of a track in decibels (-60 to +6 dB).",
            props, {"track", "volume_db"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["pan"] = propNumber("Pan position: -1.0 (full left) to 1.0 (full right), 0.0 center.", -1.0, 1.0);
        tools.append(makeTool("set_track_pan",
            "Set the pan position of a track.",
            props, {"track", "pan"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["mono"] = prop("boolean", "True for mono, false for stereo.");
        tools.append(makeTool("set_track_mono",
            "Set a track to mono or stereo mode.",
            props, {"track", "mono"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["enabled"] = prop("boolean", "True to arm for recording, false to disarm.");
        tools.append(makeTool("set_track_record_enabled",
            "Arm or disarm a track for recording.",
            props, {"track", "enabled"}));
    }

    // ── Routing ─────────────────────────────────────────────────────────────

    tools.append(makeTool("get_available_inputs",
        "List all available audio input devices that can be assigned to tracks.",
        {}));

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["input_name"] = prop("string", "The name of the input device to assign.");
        tools.append(makeTool("assign_input_to_track",
            "Route an audio input device to a track.",
            props, {"track", "input_name"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        tools.append(makeTool("clear_track_input",
            "Remove the input assignment from a track.",
            props, {"track"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        QJsonObject destProp;
        destProp["description"] = "Destination: \"master\" or a track name/index to route to.";
        QJsonArray oneOf;
        QJsonObject strType; strType["type"] = "string";
        QJsonObject intType; intType["type"] = "integer";
        oneOf.append(strType);
        oneOf.append(intType);
        destProp["oneOf"] = oneOf;
        props["destination"] = destProp;
        tools.append(makeTool("set_track_output",
            "Route a track's output to the master bus or to another track/bus.",
            props, {"track", "destination"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        tools.append(makeTool("clear_track_output",
            "Disconnect a track's output. This is a destructive action.",
            props, {"track"}));
    }

    // ── Effects ─────────────────────────────────────────────────────────────

    tools.append(makeTool("list_available_effects",
        "List all available effects: built-in (Reverb, EQ, Compressor, Delay, Chorus, Phaser, Low Pass Filter, Pitch Shift) and any installed VST plugins.",
        {}));

    {
        QJsonObject props;
        props["track"] = trackProp();
        tools.append(makeTool("get_track_effects",
            "List all effects on a track with their parameters and current values.",
            props, {"track"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        props["effect_name"] = prop("string", "The name of the effect to add (e.g. 'Reverb', 'EQ', 'Compressor', 'Delay', 'Chorus', 'Phaser', 'Low Pass Filter', 'Pitch Shift').");
        tools.append(makeTool("add_effect_to_track",
            "Add a built-in effect to a track.",
            props, {"track", "effect_name"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        QJsonObject effectRef;
        effectRef["description"] = "Effect name (string) or zero-based index in the effect chain (integer).";
        QJsonArray oneOf;
        QJsonObject strType; strType["type"] = "string";
        QJsonObject intType; intType["type"] = "integer";
        oneOf.append(strType);
        oneOf.append(intType);
        effectRef["oneOf"] = oneOf;
        props["effect"] = effectRef;
        tools.append(makeTool("remove_effect_from_track",
            "Remove an effect from a track. This is a destructive action.",
            props, {"track", "effect"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        QJsonObject effectRef;
        effectRef["description"] = "Effect name (string) or zero-based index in the effect chain (integer).";
        QJsonArray oneOf;
        QJsonObject strType; strType["type"] = "string";
        QJsonObject intType; intType["type"] = "integer";
        oneOf.append(strType);
        oneOf.append(intType);
        effectRef["oneOf"] = oneOf;
        props["effect"] = effectRef;
        QJsonObject paramRef;
        paramRef["description"] = "Parameter name (string) or zero-based index (integer).";
        QJsonArray paramOneOf;
        QJsonObject paramStrType; paramStrType["type"] = "string";
        QJsonObject paramIntType; paramIntType["type"] = "integer";
        paramOneOf.append(paramStrType);
        paramOneOf.append(paramIntType);
        paramRef["oneOf"] = paramOneOf;
        props["parameter"] = paramRef;
        props["value"] = propNumber("Normalized parameter value from 0.0 to 1.0.", 0.0, 1.0);
        tools.append(makeTool("set_effect_parameter",
            "Set a parameter on an effect. Values are normalized 0.0 to 1.0. Use get_track_effects first to see parameter names and current values.",
            props, {"track", "effect", "parameter", "value"}));
    }

    {
        QJsonObject props;
        props["track"] = trackProp();
        QJsonObject effectRef;
        effectRef["description"] = "Effect name (string) or zero-based index in the effect chain (integer).";
        QJsonArray oneOf;
        QJsonObject strType; strType["type"] = "string";
        QJsonObject intType; intType["type"] = "integer";
        oneOf.append(strType);
        oneOf.append(intType);
        effectRef["oneOf"] = oneOf;
        props["effect"] = effectRef;
        props["bypassed"] = prop("boolean", "True to bypass (disable), false to enable.");
        tools.append(makeTool("set_effect_bypass",
            "Bypass or enable an effect on a track.",
            props, {"track", "effect", "bypassed"}));
    }

    // ── Transport ───────────────────────────────────────────────────────────

    tools.append(makeTool("play",
        "Start playback.",
        {}));

    tools.append(makeTool("stop",
        "Stop playback.",
        {}));

    tools.append(makeTool("record",
        "Start recording on all armed tracks.",
        {}));

    {
        QJsonObject props;
        props["seconds"] = propNumber("Position in seconds to seek to.", 0.0, 999999.0);
        tools.append(makeTool("set_position",
            "Seek the transport to a specific position in seconds.",
            props, {"seconds"}));
    }

    tools.append(makeTool("get_transport_state",
        "Get the current transport state: playing/stopped/recording, position in seconds, and loop status.",
        {}));

    {
        QJsonObject props;
        props["bpm"] = propNumber("Tempo in beats per minute.", 20.0, 999.0);
        tools.append(makeTool("set_tempo",
            "Set the project tempo in BPM.",
            props, {"bpm"}));
    }

    {
        QJsonObject props;
        props["numerator"] = propNumber("Time signature numerator (e.g. 4).", 1.0, 32.0);
        props["denominator"] = propNumber("Time signature denominator (e.g. 4).", 1.0, 32.0);
        tools.append(makeTool("set_time_signature",
            "Set the project time signature.",
            props, {"numerator", "denominator"}));
    }

    // ── Project ─────────────────────────────────────────────────────────────

    tools.append(makeTool("save_project",
        "Save the current project to disk.",
        {}));

    tools.append(makeTool("undo",
        "Undo the last action.",
        {}));

    tools.append(makeTool("redo",
        "Redo the last undone action.",
        {}));

    return tools;
}

} // namespace freedaw
