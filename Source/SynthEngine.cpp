#include "SynthEngine.h"

namespace vms
{
// =====================================================================  ParamCache
void ParamCache::init (juce::AudioProcessorValueTreeState& s)
{
    state = &s;
    for (auto* p : s.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = r->getParameterID();
            auto* raw = s.getRawParameterValue (id);
            if (id.length() > 2 && id[1] == '_' && id[0] >= 'A' && id[0] < 'A' + kLayers)
            {
                const int l = id[0] - 'A';
                const std::string key = id.substring (2).toStdString();
                layer[(size_t) l].p[key] = raw; layer[(size_t) l].r[key] = r;
            }
            else { global[id.toStdString()] = raw; globalR[id.toStdString()] = r; }
        }
}
float ParamCache::get (const char* id) const { auto it = global.find (id); return it != global.end() ? it->second->load() : 0.0f; }
juce::RangedAudioParameter* ParamCache::param (const char* id) const { auto it = globalR.find (id); return it != globalR.end() ? it->second : nullptr; }

// tiny no‑allocation id builder:  ID ("lfo", 1, "Rate") -> "lfo1Rate"
struct ID
{
    char b[32];
    ID (const char* a, int n, const char* c = "") { std::snprintf (b, sizeof (b), "%s%d%s", a, n, c); }
    operator const char*() const { return b; }
};

// =====================================================================  Arpeggiator
void Arpeggiator::noteOn (int n, float v, bool latch)
{
    if (latch && held.empty()) latched.clear();      // a fresh chord replaces the latched one
    held.push_back ({ n, v, orderCounter++ });
    if (latch) { latched.erase (std::remove_if (latched.begin(), latched.end(), [n] (const H& h) { return h.note == n; }), latched.end()); latched.push_back ({ n, v, orderCounter }); }
}
void Arpeggiator::noteOff (int n, bool)
{
    held.erase (std::remove_if (held.begin(), held.end(), [n] (const H& h) { return h.note == n; }), held.end());
}
void Arpeggiator::allOff (std::vector<Event>& out, int offset)
{
    for (auto& p : playing) out.push_back ({ false, p.note, 0, offset });
    playing.clear(); pendingRatchets.clear();
}
void Arpeggiator::process (int n, double fs, double bpm, int mode, int rateIdx, int octaves, float gate, float swing, float prob,
                           int ratchet, float ratchetProb, std::vector<Event>& out, juce::Random& rng)
{
    const auto& src = ! held.empty() ? held : latched;
    const double stepLen = syncBeats[juce::jlimit (0, 12, rateIdx)] * 60.0 / std::max (20.0, bpm) * fs;
    const double end = counter + n;

    // release finished notes / fire pending ratchets
    auto flush = [&] (double upTo)
    {
        for (auto it = pendingRatchets.begin(); it != pendingRatchets.end();)
            if (it->first < upTo)
            {
                const int off = juce::jlimit (0, n - 1, (int) (it->first - counter));
                out.push_back ({ true, it->second.first, it->second.second, off });
                playing.push_back ({ it->second.first, it->first + stepLen * gate / std::max (1, ratchet) });
                it = pendingRatchets.erase (it);
            }
            else ++it;
        for (auto it = playing.begin(); it != playing.end();)
            if (it->offAt < upTo) { out.push_back ({ false, it->note, 0, juce::jlimit (0, n - 1, (int) (it->offAt - counter)) }); it = playing.erase (it); }
            else ++it;
    };

    if (src.empty()) { flush (end); allOff (out, 0); counter = end; nextStep = end; step = 0; seqIdx = 0; return; }
    if (nextStep < counter) nextStep = counter;

    while (nextStep < end)
    {
        flush (nextStep);
        // build the note pool
        std::vector<H> pool = src;
        if (mode == 4) std::sort (pool.begin(), pool.end(), [] (const H& a, const H& b) { return a.order < b.order; });
        else std::sort (pool.begin(), pool.end(), [] (const H& a, const H& b) { return a.note < b.note; });
        std::vector<H> seq;
        for (int o = 0; o < octaves; ++o) for (auto h : pool) { h.note += 12 * o; if (h.note <= 127) seq.push_back (h); }
        if (mode == 1) std::reverse (seq.begin(), seq.end());

        std::vector<H> toPlay;
        if (mode == 5) toPlay = pool;   // chord
        else if (! seq.empty())
        {
            int idx;
            if (mode == 3) idx = rng.nextInt ((int) seq.size());
            else if (mode == 2)
            {
                const int m = (int) seq.size();
                const int cyc = std::max (1, 2 * m - 2);
                const int k = seqIdx % cyc; idx = k < m ? k : cyc - k;
            }
            else idx = seqIdx % (int) seq.size();
            toPlay.push_back (seq[(size_t) idx]);
            ++seqIdx;
        }

        double t = nextStep + ((step & 1) ? swing * stepLen * 0.5 : 0.0);
        if (rng.nextFloat() <= prob && ! toPlay.empty())
        {
            const int reps = (ratchet > 1 && rng.nextFloat() < ratchetProb) ? ratchet : 1;
            const double sub = stepLen / reps;
            for (int r = 0; r < reps; ++r)
                for (auto& h : toPlay)
                {
                    const double at = t + r * sub;
                    if (at < end) { out.push_back ({ true, h.note, h.vel, juce::jlimit (0, n - 1, (int) (at - counter)) }); playing.push_back ({ h.note, at + sub * gate }); }
                    else pendingRatchets.push_back ({ at, { h.note, h.vel } });
                }
        }
        ++step;
        nextStep += stepLen;
    }
    flush (end);
    counter = end;
}

// =====================================================================  Engine
void SynthEngine::init (juce::AudioProcessorValueTreeState& s)
{
    pc.init (s);
    for (auto& a : liveMod) a.store (0.0f);
    for (auto& a : layerPeak) a.store (0.0f);
    lfoValue[0].store (0); lfoValue[1].store (0);
    for (auto& sp : samples) sp.store (nullptr);
    WavetableBank::get();     // build tables up front

    // fallback source for Sample/Granular when nothing is loaded: 3 s evolving formant pad at 110 Hz (A2)
    fallback = std::make_unique<SampleData>();
    fallback->rate = 44100; fallback->name = "(built-in)";
    const int len = 44100 * 3;
    fallback->audio.setSize (2, len);
    float ph = 0;
    for (int i = 0; i < len; ++i)
    {
        const float pos = (float) i / len;
        ph += 110.0f / 44100.0f; if (ph >= 1) ph -= 1;
        const float v = 0.6f * WavetableBank::get().sample (3, pos, ph, 110.0f / 44100.0f)
                      + 0.25f * WavetableBank::get().sample (1, 0.5f + 0.5f * std::sin (pos * 6.0f), std::fmod (ph * 2.0f, 1.0f), 220.0f / 44100.0f);
        fallback->audio.setSample (0, i, v); fallback->audio.setSample (1, i, v);
    }
}

void SynthEngine::prepare (double sampleRate, int block)
{
    fs = sampleRate; maxBlock = block;
    for (auto& lv : voices) for (auto& v : lv) { v.active = false; v.amp.kill(); v.env2.kill(); }
    for (auto& c : layerChorus) c.prepare (fs);
    masterChorus.prepare (fs);
    delay.prepare (fs); reverb.prepare (fs);
    layerBuf.setSize (2 * kLayers, block); sendDly.setSize (2, block); sendRev.setSize (2, block); tmp.setSize (2, block);
    for (auto& e : layerEqSet) e = { 99.0f, 99.0f };
    masterEqSet = { 99, 99, 99 };
    masterGain.reset (fs, 0.05); masterGain.setCurrentAndTargetValue (1.0f);
    arp.reset();
}

void SynthEngine::setSample (int layer, std::unique_ptr<SampleData> s)
{
    samples[(size_t) layer].store (s.get());
    sampleKeepAlive.push_back (std::move (s));   // freed with the engine (never while the audio thread may use it)
}

void SynthEngine::readSnapshot()
{
    for (int a = 0; a < kLayers; ++a)
    {
        const auto& LP = pc.layer[(size_t) a];
        auto g = [&] (const char* n) { return LP.v (n); };
        auto gi = [&] (const char* n) { return juce::roundToInt (LP.v (n)); };
        auto norm = [&] (const char* n) { return LP.rp (n)->convertTo0to1 (LP.v (n)); };
        auto& s = snap[(size_t) a];
        s.on = g ("on") > 0.5f; s.engine = gi ("engine"); s.levelN = norm ("level"); s.panN = norm ("pan");
        s.transpose = gi ("transpose"); s.fine = g ("fine"); s.keyLo = gi ("keyLo"); s.keyHi = gi ("keyHi");
        s.velLo = gi ("velLo"); s.velHi = gi ("velHi"); s.velFade = gi ("velFade"); s.poly = gi ("poly"); s.glide = g ("glide");
        s.unison = gi ("unison"); s.detune = g ("detune"); s.spread = g ("spread");
        s.timbreN = norm ("timbre"); s.wave1 = gi ("wave1"); s.wave2 = gi ("wave2"); s.semi2 = gi ("semi2"); s.pw = g ("pw"); s.sub = g ("sub"); s.noise = g ("noise");
        s.table = gi ("table"); s.fmRatio = g ("fmRatio"); s.fmFeedback = g ("fmFeedback"); s.root = gi ("root"); s.loop = g ("loop") > 0.5f;
        s.grainSize = g ("grainSize"); s.density = g ("density"); s.spray = g ("spray");
        s.f1Type = gi ("f1Type"); s.f1CutN = norm ("f1Cut"); s.f1Res = g ("f1Res"); s.f1Drive = g ("f1Drive"); s.f1Env = g ("f1Env"); s.f1Key = g ("f1Key");
        s.f2Type = gi ("f2Type"); s.f2CutN = norm ("f2Cut"); s.f2Res = g ("f2Res"); s.routing = gi ("routing");
        s.aA = g ("aA"); s.aD = g ("aD"); s.aS = g ("aS"); s.aR = g ("aR"); s.eA = g ("eA"); s.eD = g ("eD"); s.eS = g ("eS"); s.eR = g ("eR");
        s.drive = g ("drive"); s.chorus = g ("chorus"); s.eqLow = g ("eqLow"); s.eqHigh = g ("eqHigh");
        s.dlySend = g ("dlySend"); s.revSendN = norm ("revSend"); s.output = gi ("output"); s.arp = g ("arp") > 0.5f;
    }
    for (int i = 0; i < kModSlots; ++i)
    {
        slots[(size_t) i] = { juce::roundToInt (pc.get (ID ("mod", i, "Src"))), juce::roundToInt (pc.get (ID ("mod", i, "Dst"))), pc.get (ID ("mod", i, "Amt")) };
    }
    std::array<std::pair<float, float>, kMsegPoints> pts;
    for (int i = 0; i < kMsegPoints; ++i) pts[(size_t) i] = { pc.get (ID ("msegT", i)), pc.get (ID ("msegL", i)) };
    std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    for (int i = 0; i < kMsegPoints; ++i) { msegT[(size_t) i] = pts[(size_t) i].first; msegL[(size_t) i] = pts[(size_t) i].second; }
    msegLoop = pc.get ("msegLoop") > 0.5f;
    for (int i = 0; i < kMacros; ++i) macros[(size_t) i] = pc.get (ID ("macro", i + 1));
}

float SynthEngine::msegAt (float t) const
{
    if (t <= msegT[0]) return msegL[0];
    for (int i = 1; i < kMsegPoints; ++i)
        if (t <= msegT[(size_t) i])
        {
            const float span = std::max (1.0e-5f, msegT[(size_t) i] - msegT[(size_t) i - 1]);
            return msegL[(size_t) i - 1] + (msegL[(size_t) i] - msegL[(size_t) i - 1]) * (t - msegT[(size_t) i - 1]) / span;
        }
    return msegL[kMsegPoints - 1];
}

float SynthEngine::sourceValue (int src, const Voice* v) const
{
    switch (src)
    {
        case SrcLfo1: return lfoOut[0];
        case SrcLfo2: return lfoOut[1];
        case SrcSeq: return seqOut;
        case SrcModWheel: return modWheel;
        case SrcAftertouch: return aftertouch;
        case SrcMacro1: case SrcMacro2: case SrcMacro3: case SrcMacro4: return macros[(size_t) (src - SrcMacro1)];
        case SrcMseg: return v ? v->lastMseg : 0.0f;
        case SrcEnv2: return v ? v->env2.value : 0.0f;
        case SrcVelocity: return v ? v->velocity : 0.0f;
        case SrcKey: return v ? (float) (v->note - 60) / 60.0f : 0.0f;
        default: return 0.0f;
    }
}

void SynthEngine::controlTick (int n, double bpm, bool hostPlaying, double ppq)
{
    const double beatsPerSample = bpm / 60.0 / fs;
    freeBeats += n * beatsPerSample;
    const double beats = hostPlaying ? ppq : freeBeats;
    for (int i = 0; i < 2; ++i)
    {
        const bool sync = pc.get (ID ("lfo", i + 1, "Sync")) > 0.5f;
        const double sb = syncBeats[juce::jlimit (0, 12, juce::roundToInt (pc.get (ID ("lfo", i + 1, "Beat"))))];
        const float rate = sync ? (float) (bpm / 60.0 / sb) : pc.get (ID ("lfo", i + 1, "Rate"));
        float prev = lfoPhase[i];
        if (sync && hostPlaying) lfoPhase[i] = (float) std::fmod (beats / sb, 1.0);
        else { lfoPhase[i] += rate * (float) n / (float) fs; lfoPhase[i] -= std::floor (lfoPhase[i]); }
        const float ph = lfoPhase[i];
        float v = 0;
        switch (juce::roundToInt (pc.get (ID ("lfo", i + 1, "Wave"))))
        {
            case 0: v = std::sin (ph * kTwoPi); break;
            case 1: v = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
            case 2: v = 2.0f * ph - 1.0f; break;
            case 3: v = 1.0f - 2.0f * ph; break;
            case 4: v = ph < 0.5f ? 1.0f : -1.0f; break;
            default: if (ph < prev) lfoSH[i] = rng.nextFloat() * 2.0f - 1.0f; v = lfoSH[i]; break;
        }
        lfoOut[i] = v; lfoValue[i].store (v);
    }
    const int len = juce::jlimit (1, kSeqSteps, juce::roundToInt (pc.get ("seqLen")));
    const double stepBeats = syncBeats[juce::jlimit (0, 12, juce::roundToInt (pc.get ("seqBeat")))];
    const int step = (int) std::floor (beats / stepBeats) % len;
    const float target = pc.get (ID ("seq", std::max (0, step)));
    const float smooth = pc.get ("seqGlide");
    seqOut += (target - seqOut) * (smooth <= 0.001f ? 1.0f : std::min (1.0f, (float) n / (float) (fs * smooth * stepBeats * 60.0 / bpm * 0.5 + 1)));
    seqValue.store (seqOut);
}

// ---------------------------------------------------------------- notes
void SynthEngine::voiceOn (int a, int note, float vel)
{
    const auto& s = snap[(size_t) a];
    if (! s.on || note < s.keyLo || note > s.keyHi) return;
    const int v127 = juce::roundToInt (vel * 127.0f);
    if (v127 < s.velLo || v127 > s.velHi) return;
    float vg = 1.0f;
    if (s.velFade > 0)
    {
        if (s.velLo > 1)   vg *= juce::jlimit (0.0f, 1.0f, (float) (v127 - s.velLo + 1) / (float) s.velFade);
        if (s.velHi < 127) vg *= juce::jlimit (0.0f, 1.0f, (float) (s.velHi - v127 + 1) / (float) s.velFade);
    }
    auto& lv = voices[(size_t) a];
    const int poly = juce::jlimit (1, kVoicesPerLayer, s.poly);

    Voice* target = nullptr;
    int activeCount = 0;
    for (int i = 0; i < poly; ++i) if (lv[(size_t) i].active) ++activeCount;
    // same note retrigger
    for (int i = 0; i < poly; ++i) if (lv[(size_t) i].active && lv[(size_t) i].note == note) target = &lv[(size_t) i];
    if (! target) for (int i = 0; i < poly; ++i) if (! lv[(size_t) i].active) { target = &lv[(size_t) i]; break; }
    if (! target)
    {
        const int mode = juce::roundToInt (pc.get ("steal"));
        float best = 1.0e30f;
        for (int i = 0; i < poly; ++i)
        {
            auto& v = lv[(size_t) i];
            const float score = mode == 0 ? (float) v.age : mode == 1 ? v.lastLevel : mode == 2 ? (float) v.note : -(float) v.note;
            if (score < best) { best = score; target = &v; }
        }
    }
    const float newFreq = 440.0f * std::pow (2.0f, (note - 69) / 12.0f);
    const bool legato = (poly == 1 && target->active && target->amp.stage != Adsr::Release && target->amp.stage != Adsr::Idle);
    auto& v = *target;
    v.glideFrom = (s.glide > 0.0005f && lastNoteFreq[(size_t) a] > 0) ? (legato ? v.freq : lastNoteFreq[(size_t) a]) : newFreq;
    v.glidePos = s.glide > 0.0005f ? 0.0f : 1.0f;
    v.freq = newFreq; lastNoteFreq[(size_t) a] = newFreq;
    v.layer = a; v.note = note; v.velocity = vel; v.velGain = vg; v.age = ++ageCounter;
    if (! legato)
    {
        v.amp.setParams (s.aA, s.aD, s.aS, s.aR, (float) fs); v.env2.setParams (s.eA, s.eD, s.eS, s.eR, (float) fs);
        v.amp.noteOn(); v.env2.noteOn();
        v.msegTime = 0;
        v.rngState = (juce::uint32) (note * 7919 + (int) v.age * 104729);
        for (int u = 0; u < kMaxUnison; ++u) { v.ph1[(size_t) u] = v.rnd(); v.ph2[(size_t) u] = v.rnd(); v.phMod[(size_t) u] = 0; v.fmPrev[(size_t) u] = 0; }
        v.subPh = 0;
        const auto* smp = samples[(size_t) a].load(); if (! smp) smp = fallback.get();
        v.samplePos = snap[(size_t) a].timbreN * smp->audio.getNumSamples() * 0.95;
        for (auto& g : v.grains) g.on = false; v.grainClock = 0;
        if (! v.active) for (int c = 0; c < 2; ++c) { v.f1[c].reset(); v.f2[c].reset(); }
    }
    v.active = true;
}

void SynthEngine::voiceOff (int a, int note)
{
    for (auto& v : voices[(size_t) a])
        if (v.active && v.note == note && v.amp.stage != Adsr::Release) { v.amp.noteOff(); v.env2.noteOff(); }
}

void SynthEngine::routeNoteOn (int note, float vel, int mask)
{
    if (heldCount == 0)
        for (int i = 0; i < 2; ++i) if (pc.get (ID ("lfo", i + 1, "Retrig")) > 0.5f) lfoPhase[i] = 0;
    for (int a = 0; a < kLayers; ++a) if (mask & (1 << a)) voiceOn (a, note, vel);
}
void SynthEngine::routeNoteOff (int note, int mask)
{
    for (int a = 0; a < kLayers; ++a) if (mask & (1 << a)) voiceOff (a, note);
}

void SynthEngine::handleMidi (const juce::MidiMessage& m, int)
{
    int arpMask = 0, directMask = 0;
    const bool arpOn = pc.get ("arpOn") > 0.5f;
    for (int a = 0; a < kLayers; ++a) ((arpOn && snap[(size_t) a].arp) ? arpMask : directMask) |= (1 << a);

    if (m.isNoteOn())
    {
        const int key = m.getNoteNumber(); const float vel = m.getFloatVelocity();
        heldKeys.push_back (key);
        // chord memory: capture the first chord (2+ keys) played, then single keys play it
        std::vector<int> notes { key };
        if (pc.get ("chordMem") > 0.5f)
        {
            if (! chordCaptured)
            {
                if (heldKeys.size() >= 2)
                {
                    std::vector<int> k = heldKeys; std::sort (k.begin(), k.end());
                    chordBuild.clear(); for (int n : k) chordBuild.push_back (n - k.front());
                }
            }
            else
            {
                notes.clear(); for (int iv : chordIntervals) if (key + iv <= 127) notes.push_back (key + iv);
            }
        }
        expanded[key] = notes;
        ++heldCount;
        for (int n : notes)
        {
            routeNoteOn (n, vel, directMask);
            if (arpMask) arp.noteOn (n, vel, pc.get ("arpLatch") > 0.5f);
        }
    }
    else if (m.isNoteOff())
    {
        const int key = m.getNoteNumber();
        heldKeys.erase (std::remove (heldKeys.begin(), heldKeys.end(), key), heldKeys.end());
        if (pc.get ("chordMem") > 0.5f && ! chordCaptured && heldKeys.empty() && chordBuild.size() >= 2) { chordIntervals = chordBuild; chordCaptured = true; }
        heldCount = std::max (0, heldCount - 1);
        auto it = expanded.find (key);
        const auto notes = it != expanded.end() ? it->second : std::vector<int> { key };
        if (it != expanded.end()) expanded.erase (it);
        for (int n : notes) { routeNoteOff (n, directMask | (arpOn ? 0 : arpMask)); arp.noteOff (n, false); }
    }
    else if (m.isController())
    {
        if (m.getControllerNumber() == 1) modWheel = m.getControllerValue() / 127.0f;
        if (m.getControllerNumber() == 123 || m.getControllerNumber() == 120) allNotesOff();
        if (m.getControllerNumber() == 64 && m.getControllerValue() < 64) {}
    }
    else if (m.isChannelPressure()) aftertouch = m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch()) aftertouch = m.getAfterTouchValue() / 127.0f;
    else if (m.isPitchWheel()) bend = (m.getPitchWheelValue() - 8192) / 8192.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
}

void SynthEngine::allNotesOff()
{
    for (auto& lv : voices) for (auto& v : lv) if (v.active) { v.amp.noteOff(); v.env2.noteOff(); }
    arp.reset(); heldKeys.clear(); heldCount = 0; expanded.clear();
}

// ---------------------------------------------------------------- voice rendering
void SynthEngine::renderVoice (Voice& v, float* outL, float* outR, int n)
{
    const auto& s = snap[(size_t) v.layer];
    const auto& LP = pc.layer[(size_t) v.layer];

    // ---- modulation for this voice (control rate)
    std::array<float, NumLayerDests> mod {};
    v.lastMseg = msegAt (v.msegTime);
    for (const auto& sl : slots)
    {
        if (sl.src == SrcNone || sl.dst <= 0 || sl.amt == 0.0f) continue;
        const int d = sl.dst - 1;
        if (d / NumLayerDests != v.layer || d >= kLayers * NumLayerDests) continue;
        mod[(size_t) (d % NumLayerDests)] += sl.amt * sourceValue (sl.src, &v);
    }
    for (int d = 0; d < NumLayerDests; ++d) liveMod[(size_t) destIndex (v.layer, d)].store (mod[(size_t) d]);

    const float levelDb = LP.rp ("level")->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s.levelN + mod[DLevel]));
    const float pan = juce::jlimit (0.0f, 1.0f, s.panN + mod[DPan] * 0.5f) * 2.0f - 1.0f;
    const float timbre = juce::jlimit (0.0f, 1.0f, s.timbreN + mod[DTimbre]);
    const float cut1 = LP.rp ("f1Cut")->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s.f1CutN + mod[DCut1]));
    const float res1 = juce::jlimit (0.0f, 1.0f, s.f1Res + mod[DRes1]);
    const float cut2 = LP.rp ("f2Cut")->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s.f2CutN + mod[DCut2]));
    const float bendSt = bend * (float) juce::roundToInt (pc.get ("bendRange"));

    // glide
    if (v.glidePos < 1.0f) v.glidePos = std::min (1.0f, v.glidePos + (float) n / (float) (std::max (0.001f, s.glide) * fs));
    const float baseFreq = v.glideFrom * std::pow (v.freq / v.glideFrom, v.glidePos);
    const float pitchSt = (float) s.transpose + s.fine + mod[DPitch] * 24.0f + bendSt;
    const float f0 = baseFreq * std::pow (2.0f, pitchSt / 12.0f);

    // filters (set once per control block; envelope + keytrack applied here)
    const float env2Now = v.env2.value;
    const float keyOct = (v.note - 60) / 12.0f;
    const float c1 = cut1 * std::pow (2.0f, s.f1Env * env2Now * 7.0f + s.f1Key * keyOct);
    for (int c = 0; c < 2; ++c) { v.f1[c].set (c1, res1, (float) fs, s.f1Type); v.f2[c].set (cut2, s.f2Res, (float) fs, s.f2Type); }

    const float gainLin = dbToGain (levelDb) * v.velGain * (0.25f + 0.75f * v.velocity);
    const float panL = std::cos ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f), panR = std::sin ((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

    // unison setup
    const int U = s.engine <= FM ? juce::jlimit (1, kMaxUnison, s.unison) : 1;
    float inc1[kMaxUnison], inc2[kMaxUnison], uL[kMaxUnison], uR[kMaxUnison];
    for (int u = 0; u < U; ++u)
    {
        const float pos = U > 1 ? (float) u / (U - 1) * 2.0f - 1.0f : 0.0f;
        const float det = std::pow (2.0f, pos * s.detune * 0.5f / 12.0f);
        inc1[u] = f0 * det / (float) fs;
        inc2[u] = inc1[u] * std::pow (2.0f, s.semi2 / 12.0f);
        const float p = pos * s.spread;
        uL[u] = std::sqrt (0.5f * (1.0f - p)); uR[u] = std::sqrt (0.5f * (1.0f + p));
    }
    const float uNorm = 1.0f / std::sqrt ((float) U);
    const auto* smp = samples[(size_t) v.layer].load(); if (! smp) smp = fallback.get();
    const int sLen = smp->audio.getNumSamples();
    const float* sL = smp->audio.getReadPointer (0); const float* sR = smp->audio.getReadPointer (smp->audio.getNumChannels() > 1 ? 1 : 0);
    const int effRoot = smp == fallback.get() ? 45 : s.root;     // built‑in source is recorded at A2
    const double sampleInc = std::pow (2.0, (v.note - effRoot + pitchSt) / 12.0) * smp->rate / fs;

    float peak = 0;
    for (int i = 0; i < n; ++i)
    {
        float l = 0, r = 0;
        switch (s.engine)
        {
            case VA:
            {
                for (int u = 0; u < U; ++u)
                {
                    const float o1 = VaOsc::sample (s.wave1, v.ph1[(size_t) u], inc1[u], s.pw);
                    const float o2 = VaOsc::sample (s.wave2, v.ph2[(size_t) u], inc2[u], s.pw);
                    const float o = o1 * (1.0f - timbre) + o2 * timbre;
                    l += o * uL[u]; r += o * uR[u];
                    v.ph1[(size_t) u] += inc1[u]; if (v.ph1[(size_t) u] >= 1) v.ph1[(size_t) u] -= 1;
                    v.ph2[(size_t) u] += inc2[u]; if (v.ph2[(size_t) u] >= 1) v.ph2[(size_t) u] -= 1;
                }
                l *= uNorm; r *= uNorm;
                if (s.sub > 0) { const float sb = s.sub * (v.subPh < 0.5f ? 0.7f : -0.7f); l += sb; r += sb; v.subPh += inc1[0] * 0.5f; if (v.subPh >= 1) v.subPh -= 1; }
                break;
            }
            case Wavetable:
            {
                const auto& wt = WavetableBank::get();
                for (int u = 0; u < U; ++u)
                {
                    const float o = wt.sample (s.table, timbre, v.ph1[(size_t) u], inc1[u]);
                    l += o * uL[u]; r += o * uR[u];
                    v.ph1[(size_t) u] += inc1[u]; if (v.ph1[(size_t) u] >= 1) v.ph1[(size_t) u] -= 1;
                }
                l *= uNorm; r *= uNorm;
                if (s.sub > 0) { const float sb = s.sub * std::sin (v.subPh * kTwoPi); l += sb; r += sb; v.subPh += inc1[0] * 0.5f; if (v.subPh >= 1) v.subPh -= 1; }
                break;
            }
            case FM:
            {
                const float index = timbre * 8.0f;
                for (int u = 0; u < U; ++u)
                {
                    const float m = std::sin (v.phMod[(size_t) u] * kTwoPi + s.fmFeedback * 3.0f * v.fmPrev[(size_t) u]);
                    v.fmPrev[(size_t) u] = m;
                    const float o = std::sin (v.ph1[(size_t) u] * kTwoPi + index * m);
                    l += o * uL[u]; r += o * uR[u];
                    v.ph1[(size_t) u] += inc1[u]; if (v.ph1[(size_t) u] >= 1) v.ph1[(size_t) u] -= 1;
                    v.phMod[(size_t) u] += inc1[u] * s.fmRatio; v.phMod[(size_t) u] -= std::floor (v.phMod[(size_t) u]);
                }
                l *= uNorm; r *= uNorm;
                if (s.sub > 0) { const float sb = s.sub * std::sin (v.subPh * kTwoPi); l += sb; r += sb; v.subPh += inc1[0] * 0.5f; if (v.subPh >= 1) v.subPh -= 1; }
                break;
            }
            case Sample:
            {
                if (v.samplePos < sLen - 1)
                {
                    const int i0 = (int) v.samplePos; const float fr = (float) (v.samplePos - i0);
                    l = sL[i0] + (sL[i0 + 1] - sL[i0]) * fr; r = sR[i0] + (sR[i0 + 1] - sR[i0]) * fr;
                    v.samplePos += sampleInc;
                    if (s.loop && v.samplePos >= sLen - 1)
                    {
                        const double ls = timbre * (sLen - 2) * 0.95;
                        v.samplePos = ls + std::fmod (v.samplePos - ls, std::max (1.0, (double) sLen - 1 - ls));
                    }
                }
                break;
            }
            default: // granular
            {
                const float period = (float) fs / std::max (1.0f, s.density);
                v.grainClock -= 1.0f;
                if (v.grainClock <= 0)
                {
                    v.grainClock += period * (1.0f + (v.rnd() - 0.5f) * s.spray);
                    for (auto& g : v.grains)
                        if (! g.on)
                        {
                            const float pos = juce::jlimit (0.0f, 1.0f, timbre + (v.rnd() - 0.5f) * s.spray * 0.5f);
                            g.on = true; g.age = 0; g.len = std::max (16, (int) (s.grainSize * fs));
                            g.inc = sampleInc * std::pow (2.0, (v.rnd() - 0.5f) * s.spray * 0.1);
                            g.pos = pos * (sLen - 2) * 0.98;
                            const float p = (v.rnd() * 2.0f - 1.0f) * s.spray;
                            g.panL = std::sqrt (0.5f * (1.0f - p)); g.panR = std::sqrt (0.5f * (1.0f + p));
                            break;
                        }
                }
                for (auto& g : v.grains)
                {
                    if (! g.on) continue;
                    const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) g.age / (float) g.len);
                    const int i0 = (int) g.pos; const float fr = (float) (g.pos - i0);
                    if (i0 >= 0 && i0 < sLen - 1)
                    {
                        const float x = 0.5f * ((sL[i0] + (sL[i0 + 1] - sL[i0]) * fr) + (sR[i0] + (sR[i0 + 1] - sR[i0]) * fr));
                        l += x * w * g.panL; r += x * w * g.panR;
                    }
                    g.pos += g.inc; if (g.pos >= sLen - 1) g.pos -= (sLen - 2);
                    if (++g.age >= g.len) g.on = false;
                }
                l *= 0.5f; r *= 0.5f;
                break;
            }
        }
        if (s.noise > 0) { const float nz = (v.rnd() * 2.0f - 1.0f) * s.noise * 0.5f; l += nz; r += nz; }

        // filters
        float yl, yr;
        if (s.routing == 0)
        {
            yl = v.f2[0].process (v.f1[0].process (l, s.f1Type, s.f1Drive), s.f2Type, 0.0f);
            yr = v.f2[1].process (v.f1[1].process (r, s.f1Type, s.f1Drive), s.f2Type, 0.0f);
        }
        else
        {
            const float al = v.f1[0].process (l, s.f1Type, s.f1Drive), ar = v.f1[1].process (r, s.f1Type, s.f1Drive);
            if (s.f2Type == 6) { yl = al; yr = ar; }
            else { yl = 0.5f * (al + v.f2[0].process (l, s.f2Type, 0.0f)); yr = 0.5f * (ar + v.f2[1].process (r, s.f2Type, 0.0f)); }
        }
        const float env = v.amp.next(); v.env2.next();
        const float g = env * gainLin;
        yl *= g * panL * 1.4142f; yr *= g * panR * 1.4142f;
        outL[i] += yl; outR[i] += yr;
        peak = std::max (peak, std::abs (yl) + std::abs (yr));
    }
    v.lastLevel = peak;
    // MSEG time
    const bool msegSync = pc.get ("msegSync") > 0.5f;
    const float lenSec = msegSync ? (float) (syncBeats[juce::jlimit (0, 12, juce::roundToInt (pc.get ("msegBeat")))] * 60.0 / curBpm) : pc.get ("msegLen");
    v.msegTime += (float) n / (float) (std::max (0.01f, lenSec) * fs);
    if (msegLoop) v.msegTime -= std::floor (v.msegTime); else v.msegTime = std::min (v.msegTime, 1.0f);
    if (! v.amp.active()) v.active = false;
}

// ---------------------------------------------------------------- main render
void SynthEngine::render (juce::MidiBuffer& midi, float* mainL, float* mainR, int numSamples,
                          std::array<std::pair<float*, float*>, kLayers> layerOut, double bpm, bool hostPlaying, double ppq)
{
    readSnapshot();
    if (killRequested.exchange (false))
    {
        for (auto& lv : voices) for (auto& v : lv) { v.active = false; v.amp.kill(); v.env2.kill(); }
        arp.reset(); heldKeys.clear(); heldCount = 0; expanded.clear();
        delay.reset(); reverb.reset();
    }
    curBpm = std::max (20.0, bpm);
    if (pc.get ("chordMem") <= 0.5f) { chordCaptured = false; chordIntervals.clear(); chordBuild.clear(); }

    auto it = midi.begin();
    const bool arpOn = pc.get ("arpOn") > 0.5f;
    int arpMask = 0;
    for (int a = 0; a < kLayers; ++a) if (snap[(size_t) a].arp) arpMask |= (1 << a);
    if (arpWasOn && ! arpOn) { arpEvents.clear(); arp.allOff (arpEvents, 0); for (auto& e : arpEvents) routeNoteOff (e.note, arpMask); arp.reset(); }
    arpWasOn = arpOn;

    for (int start = 0; start < numSamples; )
    {
        const int n = std::min (32, numSamples - start);
        while (it != midi.end() && (*it).samplePosition < start + n) { handleMidi ((*it).getMessage(), (*it).samplePosition); ++it; }
        controlTick (n, bpm, hostPlaying, ppq + (double) start / fs * bpm / 60.0);

        if (arpOn && arpMask)
        {
            arpEvents.clear();
            arp.process (n, fs, bpm, juce::roundToInt (pc.get ("arpMode")), juce::roundToInt (pc.get ("arpRate")), juce::roundToInt (pc.get ("arpOct")),
                         pc.get ("arpGate"), pc.get ("arpSwing"), pc.get ("arpProb"), juce::roundToInt (pc.get ("arpRatchet")), pc.get ("arpRatchetProb"), arpEvents, rng);
            for (auto& e : arpEvents) { if (e.on) routeNoteOn (e.note, e.vel, arpMask); else routeNoteOff (e.note, arpMask); }
        }

        layerBuf.clear (0, n);
        int active = 0;
        for (int a = 0; a < kLayers; ++a)
        {
            float* bl = layerBuf.getWritePointer (2 * a); float* br = layerBuf.getWritePointer (2 * a + 1);
            for (auto& v : voices[(size_t) a]) if (v.active) { renderVoice (v, bl + 0, br + 0, n); ++active; }
        }
        activeVoices.store (active);

        // global‑source modulation for layer sends + master
        // per‑layer sends and master destinations only see the global sources (LFOs, SEQ, wheel, aftertouch, macros)
        auto globalMod = [&] (int dest)
        {
            float m = 0;
            for (const auto& sl : slots)
            {
                const bool global = sl.src == SrcLfo1 || sl.src == SrcLfo2 || sl.src == SrcSeq || sl.src >= SrcModWheel;
                if (sl.dst == dest && global) m += sl.amt * sourceValue (sl.src, nullptr);
            }
            liveMod[(size_t) dest].store (m);
            return m;
        };

        sendDly.clear (0, n); sendRev.clear (0, n); tmp.clear (0, n);
        float* ml = tmp.getWritePointer (0); float* mr = tmp.getWritePointer (1);
        for (int a = 0; a < kLayers; ++a)
        {
            const auto& s = snap[(size_t) a];
            float* bl = layerBuf.getWritePointer (2 * a); float* br = layerBuf.getWritePointer (2 * a + 1);
            // layer FX: drive -> EQ -> chorus
            if (s.drive > 0.001f) { const float d = 1.0f + s.drive * 8.0f, c = 1.0f / std::tanh (d); for (int i = 0; i < n; ++i) { bl[i] = std::tanh (bl[i] * d) * c / d * 1.5f; br[i] = std::tanh (br[i] * d) * c / d * 1.5f; } }
            if (s.eqLow != layerEqSet[(size_t) a].first || s.eqHigh != layerEqSet[(size_t) a].second)
            {
                auto& e = layerEq[(size_t) a];
                e[0].set (0, fs, 200, 0.7, s.eqLow); e[1].set (0, fs, 200, 0.7, s.eqLow); e[2].set (2, fs, 5000, 0.7, s.eqHigh); e[3].set (2, fs, 5000, 0.7, s.eqHigh);
                layerEqSet[(size_t) a] = { s.eqLow, s.eqHigh };
            }
            if (s.eqLow != 0 || s.eqHigh != 0) { auto& e = layerEq[(size_t) a]; for (int i = 0; i < n; ++i) { bl[i] = e[2].process (e[0].process (bl[i])); br[i] = e[3].process (e[1].process (br[i])); } }
            layerChorus[(size_t) a].process (bl, br, n, 0.8f, 0.6f, s.chorus * 0.5f);

            const float rs = juce::jlimit (0.0f, 1.0f, s.revSendN + globalMod (destIndex (a, DRevSend)));
            float pk = 0;
            for (int i = 0; i < n; ++i)
            {
                sendDly.getWritePointer (0)[i] += bl[i] * s.dlySend; sendDly.getWritePointer (1)[i] += br[i] * s.dlySend;
                sendRev.getWritePointer (0)[i] += bl[i] * rs; sendRev.getWritePointer (1)[i] += br[i] * rs;
                pk = std::max (pk, std::max (std::abs (bl[i]), std::abs (br[i])));
            }
            float prev = layerPeak[(size_t) a].load(); while (prev < pk && ! layerPeak[(size_t) a].compare_exchange_weak (prev, pk)) {}

            auto out = layerOut[(size_t) a];
            if (s.output == 1 && out.first != nullptr)
            {
                std::copy (bl, bl + n, out.first + start); std::copy (br, br + n, out.second + start);
            }
            else for (int i = 0; i < n; ++i) { ml[i] += bl[i]; mr[i] += br[i]; }
        }

        // master FX: distortion -> chorus -> EQ, + delay & reverb returns
        const float dd = pc.get ("distDrive"), dm = pc.get ("distMix");
        if (dd > 0.001f && dm > 0.0f)
        {
            const float d = 1.0f + dd * 12.0f, c = 1.0f / std::tanh (d);
            for (int i = 0; i < n; ++i)
            {
                const float wl = std::tanh (ml[i] * d) * c * 0.7f, wr = std::tanh (mr[i] * d) * c * 0.7f;
                ml[i] += dm * (wl - ml[i]); mr[i] += dm * (wr - mr[i]);
            }
        }
        const float choMix = juce::jlimit (0.0f, 1.0f, pc.get ("choMix") + globalMod (globalDestIndex (GChorusMix)));
        masterChorus.process (ml, mr, n, pc.get ("choRate"), pc.get ("choDepth"), choMix * 0.5f);
        const float lo = pc.get ("eqLow"), mi = pc.get ("eqMid"), hi = pc.get ("eqHigh");
        if (lo != masterEqSet[0] || mi != masterEqSet[1] || hi != masterEqSet[2])
        {
            masterEq[0].set (0, fs, 120, 0.7, lo); masterEq[1].set (0, fs, 120, 0.7, lo);
            masterEq[2].set (1, fs, 1000, 0.7, mi); masterEq[3].set (1, fs, 1000, 0.7, mi);
            masterEq[4].set (2, fs, 8000, 0.7, hi); masterEq[5].set (2, fs, 8000, 0.7, hi);
            masterEqSet = { lo, mi, hi };
        }
        if (lo != 0 || mi != 0 || hi != 0)
            for (int i = 0; i < n; ++i) { ml[i] = masterEq[4].process (masterEq[2].process (masterEq[0].process (ml[i]))); mr[i] = masterEq[5].process (masterEq[3].process (masterEq[1].process (mr[i]))); }

        const float dlyMix = juce::jlimit (0.0f, 1.0f, pc.get ("dlyMix") + globalMod (globalDestIndex (GDelayMix)));
        const double dlySec = syncBeats[juce::jlimit (0, 12, juce::roundToInt (pc.get ("dlyBeat")))] * 60.0 / std::max (20.0, bpm);
        delay.process (sendDly.getReadPointer (0), sendDly.getReadPointer (1), ml, mr, n, (float) std::min (4.0, dlySec), pc.get ("dlyFeedback"),
                       pc.get ("dlyTone"), pc.get ("dlyPing") > 0.5f, dlyMix);
        const float revMix = juce::jlimit (0.0f, 1.0f, pc.get ("revMix") + globalMod (globalDestIndex (GReverbMix)));
        reverb.process (sendRev.getReadPointer (0), sendRev.getReadPointer (1), ml, mr, n, pc.get ("revSize"), pc.get ("revDamp"), pc.get ("revWidth"), revMix);

        auto* mp = pc.param ("master");
        const float masterDb = mp ? mp->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, mp->convertTo0to1 (pc.get ("master")) + globalMod (globalDestIndex (GMaster)))) : 0.0f;
        masterGain.setTargetValue (dbToGain (masterDb));
        for (int i = 0; i < n; ++i) { const float g = masterGain.getNextValue(); mainL[start + i] = ml[i] * g; mainR[start + i] = mr[i] * g; }

        start += n;
    }
}
} // namespace vms
