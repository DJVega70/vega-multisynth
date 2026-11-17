#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =====================================================================
//  Vega Productions Multisynth — parameter map
// =====================================================================
namespace vms
{
constexpr int kLayers = 4, kModSlots = 16, kMsegPoints = 8, kSeqSteps = 16, kMacros = 4;
constexpr int kVoicesPerLayer = 16, kMaxUnison = 7;

inline juce::String L (int layer, const char* name) { return juce::String::charToString ((juce::juce_wchar) ('A' + layer)) + "_" + name; }

enum Engine { VA = 0, Wavetable, FM, Sample, Granular };
inline const juce::StringArray& engineNames() { static juce::StringArray a { "Virtual Analog", "Wavetable", "FM", "Sample", "Granular" }; return a; }
inline const juce::StringArray& vaWaves()     { static juce::StringArray a { "Saw", "Square", "Triangle", "Sine" }; return a; }
inline const juce::StringArray& wtNames()     { static juce::StringArray a { "Basic Shapes", "Harmonic Sweep", "PWM", "Formant", "Bell", "Digital" }; return a; }
inline const juce::StringArray& filterTypes() { static juce::StringArray a { "LP 12", "LP 24", "HP 12", "HP 24", "Band Pass", "Notch", "Off" }; return a; }
inline const juce::StringArray& routings()    { static juce::StringArray a { "Serial", "Parallel" }; return a; }
inline const juce::StringArray& outputs()     { static juce::StringArray a { "Main", "Separate" }; return a; }
inline const juce::StringArray& lfoWaves()    { static juce::StringArray a { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold" }; return a; }
inline const juce::StringArray& syncRates()   { static juce::StringArray a { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/8 D", "1/16 D" }; return a; }
inline constexpr double syncBeats[] { 16, 8, 4, 2, 1, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375 };
inline const juce::StringArray& arpModes()    { static juce::StringArray a { "Up", "Down", "Up/Down", "Random", "As Played", "Chord" }; return a; }
inline const juce::StringArray& stealModes()  { static juce::StringArray a { "Oldest", "Quietest", "Lowest", "Highest" }; return a; }

// ---- modulation sources & destinations
enum Source { SrcNone = 0, SrcLfo1, SrcLfo2, SrcMseg, SrcSeq, SrcEnv2, SrcVelocity, SrcKey, SrcModWheel, SrcAftertouch,
              SrcMacro1, SrcMacro2, SrcMacro3, SrcMacro4, NumSources };
inline const juce::StringArray& sourceNames()
{
    static juce::StringArray a { "-", "LFO 1", "LFO 2", "MSEG", "SEQ", "ENV 2", "Velocity", "Key", "Mod Wheel", "Aftertouch",
                                 "Macro 1", "Macro 2", "Macro 3", "Macro 4" };
    return a;
}
inline const juce::StringArray& sourceShort()
{
    static juce::StringArray a { "-", "LFO1", "LFO2", "MSEG", "SEQ", "ENV2", "VEL", "KEY", "MW", "AT", "M1", "M2", "M3", "M4" };
    return a;
}

// per‑layer destinations, then globals
enum LayerDest { DLevel = 0, DPan, DPitch, DTimbre, DCut1, DRes1, DCut2, DRevSend, NumLayerDests };
inline const char* layerDestParam (int d)
{
    static const char* p[] { "level", "pan", "fine", "timbre", "f1Cut", "f1Res", "f2Cut", "revSend" };
    return p[d];
}
inline const char* layerDestName (int d)
{
    static const char* n[] { "Level", "Pan", "Pitch", "Timbre", "Cutoff 1", "Reso 1", "Cutoff 2", "Reverb Send" };
    return n[d];
}
enum GlobalDest { GDelayMix = 0, GReverbMix, GChorusMix, GMaster, NumGlobalDests };
inline const char* globalDestParam (int d) { static const char* p[] { "dlyMix", "revMix", "choMix", "master" }; return p[d]; }
inline const char* globalDestName (int d)  { static const char* n[] { "Delay Mix", "Reverb Mix", "Chorus Mix", "Master" }; return n[d]; }
constexpr int kNumDests = 1 + kLayers * NumLayerDests + NumGlobalDests;   // 0 = none

inline int destIndex (int layer, int d) { return 1 + layer * NumLayerDests + d; }
inline int globalDestIndex (int d) { return 1 + kLayers * NumLayerDests + d; }
/** Parameter id a destination modulates ("" for none). */
inline juce::String destParamId (int dest)
{
    if (dest <= 0) return {};
    --dest;
    if (dest < kLayers * NumLayerDests) return L (dest / NumLayerDests, layerDestParam (dest % NumLayerDests));
    return globalDestParam (dest - kLayers * NumLayerDests);
}
inline const juce::StringArray& destNames()
{
    static juce::StringArray a = []
    {
        juce::StringArray s { "-" };
        for (int l = 0; l < kLayers; ++l)
            for (int d = 0; d < NumLayerDests; ++d) s.add (juce::String::charToString ((juce::juce_wchar) ('A' + l)) + " " + layerDestName (d));
        for (int d = 0; d < NumGlobalDests; ++d) s.add (globalDestName (d));
        return s;
    }();
    return a;
}
inline int destForParamId (const juce::String& id)
{
    for (int i = 1; i < kNumDests; ++i) if (destParamId (i) == id) return i;
    return 0;
}

inline juce::String noteName (int n)
{
    static const char* a[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    n = juce::jlimit (0, 127, n); return juce::String (a[n % 12]) + juce::String (n / 12 - 2);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto flt = [&] (const String& id, const String& name, NormalisableRange<float> r, float def, std::function<String (float, int)> f = {})
    {
        auto attr = AudioParameterFloatAttributes();
        if (f) attr = attr.withStringFromValueFunction (f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, attr));
    };
    auto integer = [&] (const String& id, const String& name, int lo, int hi, int def, std::function<String (int, int)> f = {})
    {
        auto attr = AudioParameterIntAttributes();
        if (f) attr = attr.withStringFromValueFunction (f);
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def, attr));
    };
    auto boo = [&] (const String& id, const String& name, bool def) { l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto cho = [&] (const String& id, const String& name, const StringArray& c, int def) { l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, c, def)); };
    auto logR = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };
    auto timeR = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.08f); return r; };
    auto pct = [] (float v, int) { return String (roundToInt (v * 100)) + "%"; };
    auto db = [] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; };
    auto hz = [] (float v, int) { return v >= 1000 ? String (v / 1000, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto sec = [] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; };
    auto bip = [] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v * 100)) + "%"; };
    auto note = [] (int v, int) { return noteName (v); };

    for (int a = 0; a < kLayers; ++a)
    {
        const String n = String ("Layer ") + String::charToString ((juce_wchar) ('A' + a)) + " ";
        boo (L (a, "on"), n + "On", a == 0);
        cho (L (a, "engine"), n + "Engine", engineNames(), a == 0 ? VA : (a == 1 ? Wavetable : (a == 2 ? FM : VA)));
        flt (L (a, "level"), n + "Level", { -60.0f, 6.0f, 0.01f, 2.0f }, -6.0f, db);
        flt (L (a, "pan"), n + "Pan", { -1.0f, 1.0f, 0.001f }, 0.0f, [] (float v, int) { const int p = roundToInt (v * 100); return p == 0 ? String ("C") : (p < 0 ? "L" : "R") + String (std::abs (p)); });
        integer (L (a, "transpose"), n + "Transpose", -36, 36, 0, [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; });
        flt (L (a, "fine"), n + "Fine", { -1.0f, 1.0f, 0.001f }, 0.0f, [] (float v, int) { return String (roundToInt (v * 100)) + " ct"; });
        integer (L (a, "keyLo"), n + "Key Low", 0, 127, 0, note);
        integer (L (a, "keyHi"), n + "Key High", 0, 127, 127, note);
        integer (L (a, "velLo"), n + "Vel Low", 1, 127, 1);
        integer (L (a, "velHi"), n + "Vel High", 1, 127, 127);
        integer (L (a, "velFade"), n + "Vel Fade", 0, 64, 0);
        integer (L (a, "poly"), n + "Polyphony", 1, kVoicesPerLayer, 8, [] (int v, int) { return v == 1 ? String ("Mono") : String (v); });
        flt (L (a, "glide"), n + "Glide", timeR (0.0f, 2.0f), 0.0f, sec);
        integer (L (a, "unison"), n + "Unison", 1, kMaxUnison, 1);
        flt (L (a, "detune"), n + "Unison Detune", { 0.0f, 1.0f, 0.001f }, 0.2f, pct);
        flt (L (a, "spread"), n + "Unison Spread", { 0.0f, 1.0f, 0.001f }, 0.7f, pct);
        // engine
        flt (L (a, "timbre"), n + "Timbre", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        cho (L (a, "wave1"), n + "Osc 1 Wave", vaWaves(), 0);
        cho (L (a, "wave2"), n + "Osc 2 Wave", vaWaves(), 1);
        integer (L (a, "semi2"), n + "Osc 2 Semi", -24, 24, 0, [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; });
        flt (L (a, "pw"), n + "Pulse Width", { 0.05f, 0.95f, 0.001f }, 0.5f, pct);
        flt (L (a, "sub"), n + "Sub Osc", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "noise"), n + "Noise", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        cho (L (a, "table"), n + "Wavetable", wtNames(), 0);
        flt (L (a, "fmRatio"), n + "FM Ratio", { 0.5f, 16.0f, 0.5f }, 2.0f, [] (float v, int) { return String (v, 1); });
        flt (L (a, "fmFeedback"), n + "FM Feedback", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        integer (L (a, "root"), n + "Sample Root", 0, 127, 60, note);
        boo (L (a, "loop"), n + "Sample Loop", false);
        flt (L (a, "grainSize"), n + "Grain Size", timeR (0.01f, 0.5f), 0.08f, sec);
        flt (L (a, "density"), n + "Grain Density", { 1.0f, 60.0f, 0.1f }, 20.0f, [] (float v, int) { return String (roundToInt (v)) + " /s"; });
        flt (L (a, "spray"), n + "Grain Spray", { 0.0f, 1.0f, 0.001f }, 0.1f, pct);
        // filters
        cho (L (a, "f1Type"), n + "Filter 1 Type", filterTypes(), 1);
        flt (L (a, "f1Cut"), n + "Filter 1 Cutoff", logR (20.0f, 20000.0f), 8000.0f, hz);
        flt (L (a, "f1Res"), n + "Filter 1 Reso", { 0.0f, 1.0f, 0.001f }, 0.15f, pct);
        flt (L (a, "f1Drive"), n + "Filter 1 Drive", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "f1Env"), n + "Filter 1 Env", { -1.0f, 1.0f, 0.001f }, 0.0f, bip);
        flt (L (a, "f1Key"), n + "Filter 1 Keytrack", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        cho (L (a, "f2Type"), n + "Filter 2 Type", filterTypes(), 6);
        flt (L (a, "f2Cut"), n + "Filter 2 Cutoff", logR (20.0f, 20000.0f), 200.0f, hz);
        flt (L (a, "f2Res"), n + "Filter 2 Reso", { 0.0f, 1.0f, 0.001f }, 0.1f, pct);
        cho (L (a, "routing"), n + "Filter Routing", routings(), 0);
        // envelopes
        flt (L (a, "aA"), n + "Amp Attack", timeR (0.001f, 10.0f), 0.005f, sec);
        flt (L (a, "aD"), n + "Amp Decay", timeR (0.001f, 10.0f), 0.4f, sec);
        flt (L (a, "aS"), n + "Amp Sustain", { 0.0f, 1.0f, 0.001f }, 0.8f, pct);
        flt (L (a, "aR"), n + "Amp Release", timeR (0.001f, 10.0f), 0.3f, sec);
        flt (L (a, "eA"), n + "Env 2 Attack", timeR (0.001f, 10.0f), 0.005f, sec);
        flt (L (a, "eD"), n + "Env 2 Decay", timeR (0.001f, 10.0f), 0.5f, sec);
        flt (L (a, "eS"), n + "Env 2 Sustain", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "eR"), n + "Env 2 Release", timeR (0.001f, 10.0f), 0.3f, sec);
        // layer FX + routing
        flt (L (a, "drive"), n + "Drive", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "chorus"), n + "Chorus", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "eqLow"), n + "EQ Low", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
        flt (L (a, "eqHigh"), n + "EQ High", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
        flt (L (a, "dlySend"), n + "Delay Send", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
        flt (L (a, "revSend"), n + "Reverb Send", { 0.0f, 1.0f, 0.001f }, 0.15f, pct);
        cho (L (a, "output"), n + "Output", outputs(), 0);
        boo (L (a, "arp"), n + "Arp", false);
    }

    // global voice + modulation sources
    flt ("master", "Master", { -60.0f, 6.0f, 0.01f, 2.0f }, -3.0f, db);
    cho ("steal", "Voice Steal", stealModes(), 0);
    integer ("bendRange", "Bend Range", 0, 24, 2, [] (int v, int) { return String (v) + " st"; });
    for (int i = 1; i <= 2; ++i)
    {
        const String p = "lfo" + String (i);
        cho (p + "Wave", "LFO " + String (i) + " Wave", lfoWaves(), 0);
        flt (p + "Rate", "LFO " + String (i) + " Rate", logR (0.02f, 30.0f), i == 1 ? 1.0f : 0.25f, [] (float v, int) { return String (v, 2) + " Hz"; });
        boo (p + "Sync", "LFO " + String (i) + " Sync", false);
        cho (p + "Beat", "LFO " + String (i) + " Sync Rate", syncRates(), 4);
        boo (p + "Retrig", "LFO " + String (i) + " Retrigger", false);
    }
    // MSEG: 8 breakpoints (time fraction + level), length, loop
    for (int i = 0; i < kMsegPoints; ++i)
    {
        flt ("msegT" + String (i), "MSEG Time " + String (i + 1), { 0.0f, 1.0f, 0.001f }, (float) i / (kMsegPoints - 1), pct);
        const float defaults[] { 0.0f, 1.0f, 0.55f, 0.75f, 0.3f, 0.5f, 0.15f, 0.0f };
        flt ("msegL" + String (i), "MSEG Level " + String (i + 1), { 0.0f, 1.0f, 0.001f }, defaults[i], pct);
    }
    flt ("msegLen", "MSEG Length", timeR (0.05f, 16.0f), 2.0f, sec);
    boo ("msegSync", "MSEG Sync", false);
    cho ("msegBeat", "MSEG Sync Rate", syncRates(), 2);
    boo ("msegLoop", "MSEG Loop", false);
    // step sequencer (mod source)
    for (int i = 0; i < kSeqSteps; ++i)
        flt ("seq" + String (i), "SEQ Step " + String (i + 1), { -1.0f, 1.0f, 0.001f }, (i % 4 == 0) ? 1.0f : ((i % 2) ? -0.3f : 0.4f), bip);
    integer ("seqLen", "SEQ Length", 1, kSeqSteps, 16);
    cho ("seqBeat", "SEQ Rate", syncRates(), 6);
    flt ("seqGlide", "SEQ Smooth", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
    // macros
    for (int i = 1; i <= kMacros; ++i) flt ("macro" + String (i), "Macro " + String (i), { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
    // mod matrix
    for (int s = 0; s < kModSlots; ++s)
    {
        const String p = "mod" + String (s);
        cho (p + "Src", "Mod " + String (s + 1) + " Source", sourceNames(), 0);
        cho (p + "Dst", "Mod " + String (s + 1) + " Dest", destNames(), 0);
        flt (p + "Amt", "Mod " + String (s + 1) + " Amount", { -1.0f, 1.0f, 0.001f }, 0.0f, bip);
    }
    // arpeggiator
    boo ("arpOn", "Arp On", false);
    cho ("arpMode", "Arp Mode", arpModes(), 0);
    cho ("arpRate", "Arp Rate", syncRates(), 6);
    integer ("arpOct", "Arp Octaves", 1, 4, 1);
    flt ("arpGate", "Arp Gate", { 0.05f, 1.0f, 0.001f }, 0.6f, pct);
    flt ("arpSwing", "Arp Swing", { 0.0f, 0.75f, 0.001f }, 0.0f, pct);
    flt ("arpProb", "Arp Probability", { 0.0f, 1.0f, 0.001f }, 1.0f, pct);
    integer ("arpRatchet", "Arp Ratchet", 1, 4, 1, [] (int v, int) { return String (v) + "x"; });
    flt ("arpRatchetProb", "Arp Ratchet Chance", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
    boo ("arpLatch", "Arp Latch", false);
    boo ("chordMem", "Chord Memory", false);
    // master FX
    flt ("distDrive", "Distortion Drive", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
    flt ("distMix", "Distortion Mix", { 0.0f, 1.0f, 0.001f }, 1.0f, pct);
    flt ("choRate", "Chorus Rate", logR (0.05f, 5.0f), 0.6f, [] (float v, int) { return String (v, 2) + " Hz"; });
    flt ("choDepth", "Chorus Depth", { 0.0f, 1.0f, 0.001f }, 0.4f, pct);
    flt ("choMix", "Chorus Mix", { 0.0f, 1.0f, 0.001f }, 0.0f, pct);
    cho ("dlyBeat", "Delay Time", syncRates(), 11);
    flt ("dlyFeedback", "Delay Feedback", { 0.0f, 0.95f, 0.001f }, 0.35f, pct);
    flt ("dlyTone", "Delay Tone", { 0.0f, 1.0f, 0.001f }, 0.6f, pct);
    boo ("dlyPing", "Delay Ping-Pong", true);
    flt ("dlyMix", "Delay Return", { 0.0f, 1.0f, 0.001f }, 0.5f, pct);
    flt ("revSize", "Reverb Size", { 0.0f, 1.0f, 0.001f }, 0.7f, pct);
    flt ("revDamp", "Reverb Damping", { 0.0f, 1.0f, 0.001f }, 0.4f, pct);
    flt ("revWidth", "Reverb Width", { 0.0f, 1.0f, 0.001f }, 1.0f, pct);
    flt ("revMix", "Reverb Return", { 0.0f, 1.0f, 0.001f }, 0.6f, pct);
    flt ("eqLow", "Master EQ Low", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    flt ("eqMid", "Master EQ Mid", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    flt ("eqHigh", "Master EQ High", { -12.0f, 12.0f, 0.01f }, 0.0f, db);
    return l;
}
} // namespace vms
