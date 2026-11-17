#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SynthParams.h"
#include "SynthDSP.h"
#include <map>
#include <string>
#include <cstdio>

namespace vms
{
struct SampleData
{
    juce::AudioBuffer<float> audio; double rate = 44100; juce::String name, path;
};

/** Reads every parameter once per block into plain structs (cheap, lock‑free). */
struct ParamCache
{
    void init (juce::AudioProcessorValueTreeState& s);
    // std::string keys: ids are short enough for the small‑string optimisation -> no heap use on the audio thread
    float get (const char* id) const;
    juce::RangedAudioParameter* param (const char* id) const;

    struct LayerPtrs
    {
        std::map<std::string, std::atomic<float>*> p; std::map<std::string, juce::RangedAudioParameter*> r;
        float v (const char* n) const { return p.at (n)->load(); }
        juce::RangedAudioParameter* rp (const char* n) const { return r.at (n); }
    };
    std::array<LayerPtrs, kLayers> layer;
    std::map<std::string, std::atomic<float>*> global;
    std::map<std::string, juce::RangedAudioParameter*> globalR;
    juce::AudioProcessorValueTreeState* state = nullptr;
};

struct LayerSnapshot
{
    bool on = false; int engine = 0; float levelN = 0, panN = 0.5f; int transpose = 0; float fine = 0;
    int keyLo = 0, keyHi = 127, velLo = 1, velHi = 127, velFade = 0, poly = 8; float glide = 0;
    int unison = 1; float detune = 0.2f, spread = 0.7f;
    float timbreN = 0; int wave1 = 0, wave2 = 1, semi2 = 0; float pw = 0.5f, sub = 0, noise = 0;
    int table = 0; float fmRatio = 2, fmFeedback = 0; int root = 60; bool loop = false; float grainSize = 0.08f, density = 20, spray = 0.1f;
    int f1Type = 1; float f1CutN = 0.8f, f1Res = 0.1f, f1Drive = 0, f1Env = 0, f1Key = 0;
    int f2Type = 6; float f2CutN = 0.3f, f2Res = 0.1f; int routing = 0;
    float aA = 0.005f, aD = 0.4f, aS = 0.8f, aR = 0.3f, eA = 0.005f, eD = 0.5f, eS = 0, eR = 0.3f;
    float drive = 0, chorus = 0, eqLow = 0, eqHigh = 0, dlySend = 0, revSendN = 0; int output = 0; bool arp = false;
};

struct ModSlot { int src = 0, dst = 0; float amt = 0; };

struct Voice
{
    bool active = false; int layer = 0, note = 60; float velocity = 1, velGain = 1; juce::uint64 age = 0;
    float freq = 440, glideFrom = 440; float glidePos = 1;
    Adsr amp, env2;
    float msegTime = 0; float lastMseg = 0;
    std::array<float, kMaxUnison> ph1 {}, ph2 {}, phMod {}, fmPrev {};
    float subPh = 0;
    double samplePos = 0;
    struct Grain { bool on = false; double pos = 0; double inc = 1; int age = 0, len = 1; float panL = 0.7f, panR = 0.7f; };
    std::array<Grain, 24> grains; float grainClock = 0;
    FilterSlot f1[2], f2[2];
    juce::uint32 rngState = 12345;
    float lastLevel = 0;
    inline float rnd() { rngState = rngState * 1664525u + 1013904223u; return (float) (rngState >> 8) / 16777216.0f; }
};

class Arpeggiator
{
public:
    struct Event { bool on; int note; float vel; int offset; };
    void reset() { held.clear(); latched.clear(); playing.clear(); counter = 0; step = 0; nextStep = 0; dirUp = true; }
    void noteOn (int n, float v, bool latch);
    void noteOff (int n, bool latch);
    /** advance n samples, appending note events (offsets relative to block start) */
    void process (int n, double fs, double bpm, int mode, int rateIdx, int octaves, float gate, float swing, float prob,
                  int ratchet, float ratchetProb, std::vector<Event>& out, juce::Random& rng);
    void allOff (std::vector<Event>& out, int offset);
    bool hasNotes() const { return ! held.empty() || ! latched.empty(); }
private:
    struct H { int note; float vel; int order; };
    std::vector<H> held, latched; int orderCounter = 0; bool latchArmed = false;
    struct P { int note; double offAt; };
    std::vector<P> playing;
    std::vector<std::pair<double, std::pair<int, float>>> pendingRatchets;
    double counter = 0, nextStep = 0; int step = 0; bool dirUp = true; int seqIdx = 0;
};

class SynthEngine
{
public:
    void init (juce::AudioProcessorValueTreeState& s);
    void prepare (double sampleRate, int maxBlock);
    /** Renders main stereo into mainL/R and each layer into layerOut (pre master FX). */
    void render (juce::MidiBuffer& midi, float* mainL, float* mainR, int numSamples,
                 std::array<std::pair<float*, float*>, kLayers> layerOut, double bpm, bool hostPlaying, double ppq);
    void allNotesOff();
    void requestKill() { killRequested.store (true); }   // e.g. preset change: silence everything at the next block

    void setSample (int layer, std::unique_ptr<SampleData> s);
    const SampleData* getSample (int layer) const { return samples[(size_t) layer].load(); }

    std::atomic<int> activeVoices { 0 };
    std::array<std::atomic<float>, kNumDests> liveMod;     // last modulation offset per destination (for UI rings)
    std::atomic<float> lfoValue[2], seqValue { 0 }, msegValue { 0 };
    std::array<std::atomic<float>, kLayers> layerPeak;

private:
    void readSnapshot();
    void handleMidi (const juce::MidiMessage& m, int offset);
    void routeNoteOn (int note, float vel, int layerMask);
    void routeNoteOff (int note, int layerMask);
    void voiceOn (int layer, int note, float vel);
    void voiceOff (int layer, int note);
    void renderVoice (Voice& v, float* outL, float* outR, int n);
    float sourceValue (int src, const Voice* v) const;
    float msegAt (float t) const;
    void controlTick (int n, double bpm, bool hostPlaying, double ppq);

    ParamCache pc;
    double fs = 44100; int maxBlock = 512;
    std::array<LayerSnapshot, kLayers> snap;
    std::array<ModSlot, kModSlots> slots;
    std::array<std::array<Voice, kVoicesPerLayer>, kLayers> voices;
    std::array<std::atomic<SampleData*>, kLayers> samples {};
    std::vector<std::unique_ptr<SampleData>> sampleKeepAlive;
    std::unique_ptr<SampleData> fallback;
    juce::uint64 ageCounter = 0;
    std::array<float, kLayers> lastNoteFreq {};

    // global mod sources
    float lfoPhase[2] {}, lfoOut[2] {}, lfoSH[2] {}, seqOut = 0, modWheel = 0, aftertouch = 0, bend = 0;
    std::array<float, kMacros> macros {};
    double freeBeats = 0; int heldCount = 0;
    std::array<float, kMsegPoints> msegT {}, msegL {}; float msegLen = 2; bool msegLoop = false;

    // arp + chord memory
    Arpeggiator arp; juce::Random rng; std::vector<Arpeggiator::Event> arpEvents;
    bool arpWasOn = false;
    std::vector<int> chordIntervals; std::vector<int> chordBuild; bool chordCaptured = false; std::vector<int> heldKeys;
    std::map<int, std::vector<int>> expanded;    // played key -> notes actually triggered

    // per‑layer FX + buses
    std::array<Chorus, kLayers> layerChorus;
    std::array<std::array<Bq, 4>, kLayers> layerEq;    // [lowL, lowR, highL, highR]
    std::array<std::pair<float, float>, kLayers> layerEqSet {};
    juce::AudioBuffer<float> layerBuf, sendDly, sendRev, tmp;
    PingPongDelay delay; FdnReverb reverb; Chorus masterChorus;
    std::array<Bq, 6> masterEq; std::array<float, 3> masterEqSet { 99, 99, 99 };
    juce::SmoothedValue<float> masterGain;
    double curBpm = 120.0;
    std::atomic<bool> killRequested { false };
};
} // namespace vms
