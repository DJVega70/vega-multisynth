#pragma once
#include "SynthParams.h"

// =====================================================================
//  Factory presets — each tagged with a TYPE and a GENRE for the browser
// =====================================================================
namespace vms
{
struct Preset
{
    juce::String name, type, genre;
    std::vector<std::pair<juce::String, float>> values;   // only differences from the defaults
    bool factory = true;
    juce::File file;
};

inline const juce::StringArray& presetTypes()  { static juce::StringArray a { "Bass", "Keys", "Pad", "Lead", "Pluck", "Arp", "Texture", "FX" }; return a; }
inline const juce::StringArray& presetGenres() { static juce::StringArray a { "Soulful House", "Deep House", "Techno", "Ambient", "Pop", "Cinematic" }; return a; }

inline std::vector<Preset> factoryPresets()
{
    using V = std::vector<std::pair<juce::String, float>>;
    std::vector<Preset> p;
    auto add = [&] (const char* name, const char* type, const char* genre, V v) { p.push_back ({ name, type, genre, std::move (v) }); };
    // helpers for common layer setups
    auto layer = [] (char l, std::initializer_list<std::pair<const char*, float>> kv)
    {
        V v; for (auto& [k, val] : kv) v.push_back ({ juce::String::charToString ((juce::juce_wchar) l) + "_" + k, val }); return v;
    };
    auto cat = [] (std::initializer_list<V> parts) { V out; for (auto& p : parts) out.insert (out.end(), p.begin(), p.end()); return out; };

    add ("Init", "Keys", "Pop", {});

    add ("Soulful Rhodes", "Keys", "Soulful House", cat ({
        layer ('A', { { "engine", FM }, { "fmRatio", 1.0f }, { "timbre", 0.18f }, { "fmFeedback", 0.05f }, { "aA", 0.002f }, { "aD", 2.2f }, { "aS", 0.25f }, { "aR", 0.6f },
                      { "f1Type", 0 }, { "f1Cut", 6000 }, { "f1Res", 0.05f }, { "level", -8 }, { "chorus", 0.45f }, { "revSend", 0.25f }, { "poly", 12 } }),
        layer ('B', { { "on", 1 }, { "engine", FM }, { "fmRatio", 14.0f }, { "timbre", 0.08f }, { "aA", 0.001f }, { "aD", 0.25f }, { "aS", 0 }, { "aR", 0.2f }, { "level", -20 }, { "f1Type", 6 }, { "poly", 12 } }),
        { { "mod0Src", SrcVelocity }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 0.35f },
          { "mod1Src", SrcLfo1 }, { "mod1Dst", (float) destIndex (0, DPan) }, { "mod1Amt", 0.25f }, { "lfo1Rate", 4.5f }, { "revMix", 0.5f } } }));

    add ("Deep Organ Chord", "Keys", "Deep House", cat ({
        layer ('A', { { "engine", Wavetable }, { "table", 1 }, { "timbre", 0.25f }, { "aA", 0.003f }, { "aD", 0.3f }, { "aS", 0.6f }, { "aR", 0.15f },
                      { "f1Cut", 2500 }, { "f1Env", 0.35f }, { "eD", 0.25f }, { "level", -9 }, { "dlySend", 0.2f }, { "revSend", 0.3f } }),
        { { "chordMem", 0 }, { "dlyBeat", 11 }, { "dlyFeedback", 0.3f } } }));

    add ("Warm Saw Pad", "Pad", "Soulful House", cat ({
        layer ('A', { { "unison", 5 }, { "detune", 0.35f }, { "spread", 0.9f }, { "wave1", 0 }, { "wave2", 0 }, { "semi2", 12 }, { "timbre", 0.3f },
                      { "aA", 0.8f }, { "aD", 1.0f }, { "aS", 0.85f }, { "aR", 1.8f }, { "f1Cut", 1800 }, { "f1Res", 0.12f }, { "level", -12 }, { "revSend", 0.45f }, { "chorus", 0.3f } }),
        { { "mod0Src", SrcLfo2 }, { "mod0Dst", (float) destIndex (0, DCut1) }, { "mod0Amt", 0.12f }, { "lfo2Rate", 0.15f }, { "revMix", 0.7f }, { "revSize", 0.85f } } }));

    add ("Glass Wavetable Pad", "Pad", "Ambient", cat ({
        layer ('A', { { "engine", Wavetable }, { "table", 4 }, { "unison", 3 }, { "detune", 0.2f }, { "aA", 1.5f }, { "aS", 0.9f }, { "aR", 3.0f },
                      { "f1Cut", 9000 }, { "level", -12 }, { "revSend", 0.6f } }),
        layer ('B', { { "on", 1 }, { "engine", Granular }, { "timbre", 0.4f }, { "spray", 0.5f }, { "density", 30 }, { "grainSize", 0.15f }, { "aA", 2.0f }, { "aS", 1.0f }, { "aR", 4.0f },
                      { "level", -16 }, { "f1Cut", 5000 }, { "revSend", 0.8f } }),
        { { "mod0Src", SrcMseg }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 0.7f }, { "msegLoop", 1 }, { "msegLen", 6.0f },
          { "mod1Src", SrcLfo1 }, { "mod1Dst", (float) destIndex (1, DTimbre) }, { "mod1Amt", 0.3f }, { "lfo1Rate", 0.07f }, { "revMix", 0.8f }, { "revSize", 0.95f } } }));

    add ("Sub Bass", "Bass", "Deep House", cat ({
        layer ('A', { { "wave1", 3 }, { "wave2", 2 }, { "timbre", 0.25f }, { "sub", 0.3f }, { "poly", 1 }, { "glide", 0.04f }, { "aA", 0.002f }, { "aD", 0.3f }, { "aS", 0.9f }, { "aR", 0.12f },
                      { "f1Type", 1 }, { "f1Cut", 400 }, { "level", -6 }, { "revSend", 0 }, { "keyHi", 59 } }) }));

    add ("Garage Organ Bass", "Bass", "Soulful House", cat ({
        layer ('A', { { "engine", FM }, { "fmRatio", 1.0f }, { "timbre", 0.35f }, { "fmFeedback", 0.25f }, { "poly", 1 }, { "aA", 0.001f }, { "aD", 0.25f }, { "aS", 0.5f }, { "aR", 0.08f },
                      { "f1Type", 1 }, { "f1Cut", 900 }, { "f1Env", 0.4f }, { "eD", 0.2f }, { "drive", 0.25f }, { "level", -6 }, { "revSend", 0 } }),
        { { "mod0Src", SrcEnv2 }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 0.3f } } }));

    add ("Acid Line", "Bass", "Techno", cat ({
        layer ('A', { { "wave1", 0 }, { "poly", 1 }, { "glide", 0.06f }, { "aA", 0.001f }, { "aD", 0.2f }, { "aS", 0.6f }, { "aR", 0.05f },
                      { "f1Type", 1 }, { "f1Cut", 300 }, { "f1Res", 0.8f }, { "f1Env", 0.6f }, { "eD", 0.18f }, { "f1Drive", 0.4f }, { "level", -8 }, { "arp", 1 }, { "revSend", 0 } }),
        { { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 6 }, { "arpGate", 0.5f }, { "arpOct", 2 }, { "arpRatchet", 2 }, { "arpRatchetProb", 0.2f },
          { "mod0Src", SrcMacro1 }, { "mod0Dst", (float) destIndex (0, DCut1) }, { "mod0Amt", 0.5f }, { "dlySend", 0.0f } } }));

    add ("Reese Bass", "Bass", "Techno", cat ({
        layer ('A', { { "unison", 3 }, { "detune", 0.6f }, { "spread", 0.3f }, { "wave1", 0 }, { "poly", 1 }, { "aS", 1.0f }, { "aR", 0.1f },
                      { "f1Type", 1 }, { "f1Cut", 700 }, { "f1Res", 0.2f }, { "drive", 0.35f }, { "level", -9 }, { "revSend", 0 } }),
        { { "mod0Src", SrcLfo1 }, { "mod0Dst", (float) destIndex (0, DCut1) }, { "mod0Amt", 0.12f }, { "lfo1Rate", 0.3f } } }));

    add ("House Piano Stab", "Keys", "Soulful House", cat ({
        layer ('A', { { "engine", Wavetable }, { "table", 0 }, { "timbre", 0.55f }, { "aA", 0.001f }, { "aD", 0.6f }, { "aS", 0.0f }, { "aR", 0.4f },
                      { "f1Cut", 3500 }, { "f1Env", 0.3f }, { "eD", 0.35f }, { "level", -9 }, { "revSend", 0.35f }, { "dlySend", 0.15f } }),
        layer ('B', { { "on", 1 }, { "engine", FM }, { "fmRatio", 3.0f }, { "timbre", 0.25f }, { "aA", 0.001f }, { "aD", 0.4f }, { "aS", 0 }, { "aR", 0.3f }, { "level", -15 }, { "f1Type", 6 } }) }));

    add ("Supersaw Lead", "Lead", "Pop", cat ({
        layer ('A', { { "unison", 7 }, { "detune", 0.45f }, { "spread", 1.0f }, { "wave1", 0 }, { "wave2", 0 }, { "semi2", 12 }, { "timbre", 0.25f }, { "poly", 8 },
                      { "aA", 0.005f }, { "aS", 0.9f }, { "aR", 0.35f }, { "f1Cut", 7000 }, { "level", -12 }, { "revSend", 0.35f }, { "dlySend", 0.25f } }) }));

    add ("Soul Whistle Lead", "Lead", "Soulful House", cat ({
        layer ('A', { { "wave1", 3 }, { "poly", 1 }, { "glide", 0.08f }, { "aA", 0.05f }, { "aS", 1.0f }, { "aR", 0.4f }, { "f1Type", 6 }, { "noise", 0.04f }, { "level", -10 }, { "revSend", 0.4f }, { "dlySend", 0.3f } }),
        { { "mod0Src", SrcModWheel }, { "mod0Dst", (float) destIndex (0, DPitch) }, { "mod0Amt", 0.0f },
          { "mod1Src", SrcLfo1 }, { "mod1Dst", (float) destIndex (0, DPitch) }, { "mod1Amt", 0.006f }, { "lfo1Rate", 5.5f } } }));

    add ("FM Pluck", "Pluck", "Deep House", cat ({
        layer ('A', { { "engine", FM }, { "fmRatio", 2.0f }, { "timbre", 0.3f }, { "aA", 0.001f }, { "aD", 0.35f }, { "aS", 0 }, { "aR", 0.3f },
                      { "f1Cut", 5000 }, { "level", -9 }, { "dlySend", 0.3f }, { "revSend", 0.3f } }),
        { { "mod0Src", SrcEnv2 }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 0.5f }, { "A_eD", 0.15f } } }));

    add ("Marimba Pluck", "Pluck", "Ambient", cat ({
        layer ('A', { { "engine", FM }, { "fmRatio", 4.0f }, { "timbre", 0.2f }, { "aA", 0.001f }, { "aD", 0.5f }, { "aS", 0 }, { "aR", 0.4f }, { "level", -9 }, { "revSend", 0.4f } }) }));

    add ("Chord Memory Stab", "Keys", "Deep House", cat ({
        layer ('A', { { "unison", 3 }, { "detune", 0.2f }, { "aA", 0.001f }, { "aD", 0.35f }, { "aS", 0.0f }, { "aR", 0.25f },
                      { "f1Cut", 1600 }, { "f1Env", 0.5f }, { "eD", 0.2f }, { "level", -10 }, { "dlySend", 0.35f }, { "revSend", 0.3f } }),
        { { "chordMem", 1 }, { "dlyBeat", 11 } } }));

    add ("Polyrhythm Arp", "Arp", "Techno", cat ({
        layer ('A', { { "engine", Wavetable }, { "table", 5 }, { "timbre", 0.4f }, { "aA", 0.001f }, { "aD", 0.2f }, { "aS", 0.0f }, { "aR", 0.15f },
                      { "f1Cut", 3000 }, { "f1Res", 0.3f }, { "level", -10 }, { "arp", 1 }, { "dlySend", 0.35f } }),
        { { "arpOn", 1 }, { "arpMode", 2 }, { "arpRate", 6 }, { "arpOct", 3 }, { "arpProb", 0.85f }, { "arpRatchet", 3 }, { "arpRatchetProb", 0.25f }, { "arpSwing", 0.2f },
          { "mod0Src", SrcSeq }, { "mod0Dst", (float) destIndex (0, DCut1) }, { "mod0Amt", 0.25f } } }));

    add ("Dreamy Arp", "Arp", "Soulful House", cat ({
        layer ('A', { { "engine", FM }, { "fmRatio", 2.0f }, { "timbre", 0.15f }, { "aA", 0.002f }, { "aD", 0.4f }, { "aS", 0.0f }, { "aR", 0.6f }, { "level", -10 }, { "arp", 1 },
                      { "revSend", 0.5f }, { "dlySend", 0.4f } }),
        layer ('B', { { "on", 1 }, { "unison", 5 }, { "detune", 0.3f }, { "aA", 1.2f }, { "aS", 0.8f }, { "aR", 2.0f }, { "f1Cut", 1200 }, { "level", -18 }, { "revSend", 0.6f } }),
        { { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 5 }, { "arpOct", 2 }, { "arpLatch", 0 }, { "revMix", 0.7f } } }));

    add ("Split: Bass + Keys", "Keys", "Soulful House", cat ({
        layer ('A', { { "wave1", 3 }, { "sub", 0.3f }, { "poly", 1 }, { "aR", 0.1f }, { "f1Cut", 500 }, { "keyHi", 52 }, { "level", -6 }, { "revSend", 0 } }),
        layer ('B', { { "on", 1 }, { "engine", FM }, { "fmRatio", 1.0f }, { "timbre", 0.2f }, { "aD", 1.8f }, { "aS", 0.3f }, { "aR", 0.5f }, { "keyLo", 53 },
                      { "level", -9 }, { "chorus", 0.4f }, { "revSend", 0.3f } }) }));

    add ("Velocity Layers", "Keys", "Cinematic", cat ({
        layer ('A', { { "wave1", 2 }, { "aD", 1.0f }, { "aS", 0.4f }, { "f1Cut", 1500 }, { "velHi", 90 }, { "velFade", 30 }, { "level", -9 } }),
        layer ('B', { { "on", 1 }, { "wave1", 0 }, { "unison", 3 }, { "aD", 1.0f }, { "aS", 0.4f }, { "f1Cut", 6000 }, { "velLo", 70 }, { "velFade", 30 }, { "level", -9 } }) }));

    add ("Granular Drift", "Texture", "Ambient", cat ({
        layer ('A', { { "engine", Granular }, { "timbre", 0.3f }, { "density", 25 }, { "grainSize", 0.2f }, { "spray", 0.6f }, { "aA", 1.0f }, { "aS", 1.0f }, { "aR", 3.0f },
                      { "f1Cut", 4000 }, { "level", -9 }, { "revSend", 0.7f } }),
        { { "mod0Src", SrcLfo1 }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 0.4f }, { "lfo1Rate", 0.05f }, { "revMix", 0.8f } } }));

    add ("Riser Sweep", "FX", "Cinematic", cat ({
        layer ('A', { { "noise", 0.8f }, { "timbre", 0.0f }, { "wave1", 0 }, { "aA", 4.0f }, { "aS", 1.0f }, { "aR", 1.5f }, { "f1Type", 4 }, { "f1Cut", 300 }, { "f1Res", 0.5f },
                      { "level", -12 }, { "revSend", 0.6f } }),
        { { "mod0Src", SrcMseg }, { "mod0Dst", (float) destIndex (0, DCut1) }, { "mod0Amt", 0.7f }, { "msegLen", 8.0f },
          { "msegT0", 0 }, { "msegL0", 0 }, { "msegT1", 1.0f }, { "msegL1", 1.0f }, { "msegT2", 1.0f }, { "msegL2", 1.0f }, { "msegT3", 1.0f }, { "msegL3", 1.0f },
          { "msegT4", 1.0f }, { "msegL4", 1.0f }, { "msegT5", 1.0f }, { "msegL5", 1.0f }, { "msegT6", 1.0f }, { "msegL6", 1.0f }, { "msegT7", 1.0f }, { "msegL7", 1.0f } } }));

    add ("Macro Morph Pad", "Pad", "Cinematic", cat ({
        layer ('A', { { "engine", Wavetable }, { "table", 3 }, { "unison", 3 }, { "aA", 1.0f }, { "aS", 1.0f }, { "aR", 2.5f }, { "f1Cut", 2500 }, { "level", -12 }, { "revSend", 0.5f } }),
        { { "mod0Src", SrcMacro1 }, { "mod0Dst", (float) destIndex (0, DTimbre) }, { "mod0Amt", 1.0f },
          { "mod1Src", SrcMacro1 }, { "mod1Dst", (float) destIndex (0, DCut1) }, { "mod1Amt", 0.3f },
          { "mod2Src", SrcMacro2 }, { "mod2Dst", (float) globalDestIndex (GReverbMix) }, { "mod2Amt", 0.4f } } }));
    return p;
}
} // namespace vms
