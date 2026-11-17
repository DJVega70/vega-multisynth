#include "PluginEditor.h"
using namespace vms;

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;

void setParam (APVTS& s, const juce::String& id, float realValue)
{
    if (auto* p = s.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (realValue)); p->endChangeGesture(); }
}
float getParam (APVTS& s, const juce::String& id) { auto* v = s.getRawParameterValue (id); return v ? v->load() : 0.0f; }

//==============================================================================
// Drag source chip (LFO1, LFO2, MSEG, ... M4)
class SourceChip : public juce::Component
{
public:
    SourceChip (int s) : src (s) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag onto a knob to modulate it"); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff161616)); g.fillRoundedRectangle (b, 3.0f);
        g.setColour (VegaColours::accent.withAlpha (isMouseOver() ? 1.0f : 0.6f)); g.drawRoundedRectangle (b, 3.0f, 1.0f);
        g.setColour (VegaColours::accent); g.setFont (VegaFonts::ui (10.5f, true));
        g.drawText (sourceShort()[src], b.toNearestInt(), juce::Justification::centred);
    }
    void mouseDrag (const juce::MouseEvent&) override
    {
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
            if (! c->isDragAndDropActive()) c->startDragging ("vmsrc:" + juce::String (src), this);
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void setTooltip (const juce::String&) {}
    int src;
};

//==============================================================================
// Knob that accepts dropped mod sources and draws modulation rings
class ModKnob : public vegaui::Knob, public juce::DragAndDropTarget, private juce::Timer
{
public:
    ModKnob (VegaSynthProcessor& p, const juce::String& id, const juce::String& title, bool big = false)
        : vegaui::Knob (p.apvts, id, title, big), proc (p), paramId (id), dest (destForParamId (id))
    {
        if (dest > 0) startTimerHz (20);
        slider.addMouseListener (this, false);
    }
    bool isInterestedInDragSource (const SourceDetails& d) override { return dest > 0 && d.description.toString().startsWith ("vmsrc:"); }
    void itemDragEnter (const SourceDetails&) override { hover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { hover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        hover = false;
        const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
        auto& s = proc.apvts;
        for (int i = 0; i < kModSlots; ++i)   // already routed? nudge amount instead
            if (juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Src")) == src && juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Dst")) == dest) { repaint(); return; }
        for (int i = 0; i < kModSlots; ++i)
            if (juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Src")) == 0)
            {
                setParam (s, "mod" + juce::String (i) + "Src", (float) src);
                setParam (s, "mod" + juce::String (i) + "Dst", (float) dest);
                setParam (s, "mod" + juce::String (i) + "Amt", 0.5f);
                break;
            }
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (dest <= 0 || ! e.mods.isPopupMenu()) return;
        juce::PopupMenu m; auto& s = proc.apvts; int count = 0;
        for (int i = 0; i < kModSlots; ++i)
            if (juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Dst")) == dest && juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Src")) > 0)
            {
                const int src = juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Src"));
                juce::PopupMenu sub;
                for (int a : { 100, 50, 25, 10, -10, -25, -50, -100 }) sub.addItem (1000 + i * 100 + (a + 100) / 5, juce::String (a) + "%");
                sub.addSeparator(); sub.addItem (500 + i, "Remove");
                m.addSubMenu (sourceNames()[src] + "  (" + juce::String (juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Amt") * 100)) + "%)", sub);
                ++count;
            }
        if (count == 0) m.addItem (-1, "Drag an LFO / MSEG / SEQ / Macro chip here to modulate", false);
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int r)
        {
            auto& s = proc.apvts;
            if (r >= 1000) { const int slot = (r - 1000) / 100, a = ((r - 1000) % 100) * 5 - 100; setParam (s, "mod" + juce::String (slot) + "Amt", a / 100.0f); }
            else if (r >= 500) { const int slot = r - 500; setParam (s, "mod" + juce::String (slot) + "Src", 0); setParam (s, "mod" + juce::String (slot) + "Dst", 0); setParam (s, "mod" + juce::String (slot) + "Amt", 0); }
            repaint();
        });
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (dest <= 0) return;
        float sum = 0; auto& s = proc.apvts; bool any = false;
        for (int i = 0; i < kModSlots; ++i)
            if (juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Dst")) == dest && juce::roundToInt (getParam (s, "mod" + juce::String (i) + "Src")) > 0)
            { sum += getParam (s, "mod" + juce::String (i) + "Amt"); any = true; }
        auto sb = slider.getBounds().toFloat();
        auto area = sb.withTrimmedBottom (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow ? 16.0f : 0.0f);
        const float size = std::min (area.getWidth() - 18.0f, area.getHeight() - 18.0f);
        const auto c = area.getCentre();
        const float r = size * 0.5f + 5.0f;
        if (hover) { g.setColour (VegaColours::accent.withAlpha (0.35f)); g.drawEllipse (c.x - r - 3, c.y - r - 3, (r + 3) * 2, (r + 3) * 2, 2.0f); }
        if (! any) return;
        const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        const float pos = (float) slider.valueToProportionOfLength (slider.getValue());
        const float a0 = start + pos * (end - start), a1 = start + juce::jlimit (0.0f, 1.0f, pos + sum) * (end - start);
        juce::Path arc; arc.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (a0, a1), std::max (a0, a1), true);
        g.setColour (VegaColours::accent.withAlpha (0.85f)); g.strokePath (arc, juce::PathStrokeType (2.5f));
        const float live = proc.engine.liveMod[(size_t) dest].load();
        const float al = start + juce::jlimit (0.0f, 1.0f, pos + live) * (end - start);
        g.setColour (juce::Colours::white); g.fillEllipse (c.x + r * std::sin (al) - 3, c.y - r * std::cos (al) - 3, 6, 6);
    }
private:
    void timerCallback() override { repaint(); }
    VegaSynthProcessor& proc; juce::String paramId; int dest; bool hover = false;
};

std::unique_ptr<vegaui::Knob> makeKnob (VegaSynthProcessor& p, const juce::String& id, const juce::String& t, bool big = false)
{
    if (destForParamId (id) > 0) return std::make_unique<ModKnob> (p, id, t, big);
    return std::make_unique<vegaui::Knob> (p.apvts, id, t, big);
}

//==============================================================================
class EnvView : public juce::Component, private juce::Timer
{
public:
    EnvView (APVTS& s, juce::String a, juce::String d, juce::String su, juce::String r) : st (s), A (a), D (d), S (su), R (r) { startTimerHz (10); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); vegaui::drawScreen (g, b); b = b.reduced (6, 6);
        const float a = std::sqrt (getParam (st, A)), d = std::sqrt (getParam (st, D)), s = getParam (st, S), r = std::sqrt (getParam (st, R));
        const float tot = a + d + 0.6f + r, k = b.getWidth() / tot;
        juce::Path p; p.startNewSubPath (b.getX(), b.getBottom());
        p.lineTo (b.getX() + a * k, b.getY()); p.lineTo (b.getX() + (a + d) * k, b.getBottom() - s * b.getHeight());
        p.lineTo (b.getX() + (a + d + 0.6f) * k, b.getBottom() - s * b.getHeight()); p.lineTo (b.getRight(), b.getBottom());
        g.setColour (VegaColours::accent); g.strokePath (p, juce::PathStrokeType (1.6f));
        g.setColour (VegaColours::accent.withAlpha (0.12f)); p.closeSubPath(); g.fillPath (p);
    }
    void timerCallback() override { repaint(); }
    APVTS& st; juce::String A, D, S, R;
};

//==============================================================================
class LayerPanel : public juce::Component, private juce::Timer
{
public:
    LayerPanel (VegaSynthProcessor& p, int layerIndex) : proc (p), layer (layerIndex),
        engine (p.apvts, L (layerIndex, "engine"), "Engine"),
        wave1 (p.apvts, L (layerIndex, "wave1"), "Osc 1"), wave2 (p.apvts, L (layerIndex, "wave2"), "Osc 2"),
        table (p.apvts, L (layerIndex, "table"), "Wavetable"),
        loop (p.apvts, L (layerIndex, "loop"), "Loop"),
        f1Type (p.apvts, L (layerIndex, "f1Type"), "Filter 1"), f2Type (p.apvts, L (layerIndex, "f2Type"), "Filter 2"),
        routing (p.apvts, L (layerIndex, "routing"), "Routing", "Serial|Parallel"),
        output (p.apvts, L (layerIndex, "output"), "Output", "Main|Separate"),
        arpOn (p.apvts, L (layerIndex, "arp"), "Arp"),
        ampEnv (p.apvts, L (layerIndex, "aA"), L (layerIndex, "aD"), L (layerIndex, "aS"), L (layerIndex, "aR")),
        modEnv (p.apvts, L (layerIndex, "eA"), L (layerIndex, "eD"), L (layerIndex, "eS"), L (layerIndex, "eR"))
    {
        auto k = [&] (const char* id, const char* t, bool big = false) { knobs[id] = makeKnob (p, L (layer, id), t, big); addAndMakeVisible (*knobs[id]); };
        for (auto [id, t] : std::initializer_list<std::pair<const char*, const char*>> {
                 { "semi2", "Osc 2 Semi" }, { "timbre", "Mix" }, { "pw", "Pulse W" }, { "sub", "Sub" }, { "noise", "Noise" },
                 { "fmRatio", "Ratio" }, { "fmFeedback", "Feedback" }, { "root", "Root" }, { "grainSize", "Grain Size" }, { "density", "Density" }, { "spray", "Spray" },
                 { "unison", "Unison" }, { "detune", "Detune" }, { "spread", "Spread" }, { "poly", "Voices" }, { "glide", "Glide" }, { "transpose", "Transpose" }, { "fine", "Fine" }, { "level", "Level" },
                 { "f1Cut", "Cutoff" }, { "f1Res", "Reso" }, { "f1Drive", "Drive" }, { "f1Env", "Env Amt" }, { "f1Key", "Key Track" }, { "f2Cut", "Cutoff" }, { "f2Res", "Reso" },
                 { "aA", "Attack" }, { "aD", "Decay" }, { "aS", "Sustain" }, { "aR", "Release" }, { "eA", "Attack" }, { "eD", "Decay" }, { "eS", "Sustain" }, { "eR", "Release" },
                 { "drive", "Drive" }, { "chorus", "Chorus" }, { "eqLow", "EQ Low" }, { "eqHigh", "EQ High" }, { "dlySend", "Delay" }, { "revSend", "Reverb" },
                 { "keyLo", "Key Low" }, { "keyHi", "Key High" }, { "pan", "Pan" }, { "velLo", "Vel Low" }, { "velHi", "Vel High" }, { "velFade", "Vel Fade" } })
            k (id, t);
        for (auto* c : std::initializer_list<juce::Component*> { &engine, &wave1, &wave2, &table, &loop, &f1Type, &f2Type, &routing, &output, &arpOn, &ampEnv, &modEnv })
            addAndMakeVisible (c);
        loadBtn.setButtonText ("LOAD SAMPLE...");
        loadBtn.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) { juce::String err; proc.loadSample (layer, fc.getResult(), err); status = err; }
            });
        };
        addAndMakeVisible (loadBtn);
        startTimerHz (8);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        const char* t1[] { "ENGINE", "VOICE", "FILTERS" };
        const int x1[] { 0, 430, 760, getWidth() };
        for (int i = 0; i < 3; ++i) vegaui::drawSection (g, { x1[i], 0, x1[i + 1] - x1[i], 270 }, t1[i], i > 0);
        const char* t2[] { "AMP ENVELOPE", "ENVELOPE 2", "LAYER FX", "ZONE / OUTPUT" };
        const int x2[] { 0, 330, 660, 960, getWidth() };
        for (int i = 0; i < 4; ++i) vegaui::drawSection (g, { x2[i], 282, x2[i + 1] - x2[i], 300 }, t2[i], i > 0);
        g.setColour (VegaColours::line); g.drawHorizontalLine (276, 0, (float) getWidth());
        const int eng = juce::roundToInt (getParam (proc.apvts, L (layer, "engine")));
        if (eng >= Sample)
        {
            g.setFont (VegaFonts::ui (10.5f)); g.setColour (VegaColours::textDim);
            g.drawText (proc.sampleName (layer) + (status.isNotEmpty() ? "  - " + status : juce::String()), 210, 64, 215, 14, juce::Justification::left);
        }
    }

    void resized() override
    {
        auto K = [&] (const char* id) -> juce::Component& { return *knobs[id]; };
        const int kw = 82, kh = 108;
        engine.setBounds (10, 18, 190, 46);
        loadBtn.setBounds (210, 36, 140, 22);
        // engine‑specific grid (positions decided in layoutEngine)
        layoutEngine();
        // VOICE
        int vx = 436;
        juce::Component* v1[] { &K ("unison"), &K ("detune"), &K ("spread"), &K ("poly") };
        juce::Component* v2[] { &K ("glide"), &K ("transpose"), &K ("fine"), &K ("level") };
        for (int i = 0; i < 4; ++i) { v1[i]->setBounds (vx + i * kw, 20, kw, kh); v2[i]->setBounds (vx + i * kw, 140, kw, kh); }
        // FILTERS
        const int fx = 766;
        f1Type.setBounds (fx, 30, 100, 46);
        juce::Component* f1[] { &K ("f1Cut"), &K ("f1Res"), &K ("f1Drive"), &K ("f1Env"), &K ("f1Key") };
        for (int i = 0; i < 5; ++i) f1[i]->setBounds (fx + 104 + i * 76, 20, 76, kh);
        f2Type.setBounds (fx, 150, 100, 46);
        K ("f2Cut").setBounds (fx + 104, 140, 76, kh); K ("f2Res").setBounds (fx + 180, 140, 76, kh);
        routing.setBounds (fx + 270, 160, 110, 60);
        // ENVELOPES
        ampEnv.setBounds (10, 300, 310, 70); modEnv.setBounds (340, 300, 310, 70);
        const char* ae[] { "aA", "aD", "aS", "aR" }; const char* me[] { "eA", "eD", "eS", "eR" };
        for (int i = 0; i < 4; ++i) { K (ae[i]).setBounds (6 + i * 78, 378, 78, kh); K (me[i]).setBounds (336 + i * 78, 378, 78, kh); }
        // LAYER FX
        const char* lf[] { "drive", "chorus", "eqLow", "eqHigh", "dlySend", "revSend" };
        for (int i = 0; i < 6; ++i) K (lf[i]).setBounds (666 + (i % 3) * 96, 300 + (i / 3) * 132, 92, kh + 14);
        // ZONE
        const char* zn[] { "keyLo", "keyHi", "pan", "velLo", "velHi", "velFade" };
        const int zw = (getWidth() - 966) / 3;
        for (int i = 0; i < 6; ++i) K (zn[i]).setBounds (966 + (i % 3) * zw, 296 + (i / 3) * 112, zw, kh);
        output.setBounds (966, 520, 120, 54);
        arpOn.setBounds (1110, 492, 70, 74);
    }

private:
    void layoutEngine()
    {
        auto K = [&] (const char* id) -> juce::Component& { return *knobs[id]; };
        const int eng = juce::roundToInt (getParam (proc.apvts, L (layer, "engine")));
        for (auto id : { "semi2", "timbre", "pw", "sub", "noise", "fmRatio", "fmFeedback", "root", "grainSize", "density", "spray" }) K (id).setVisible (false);
        for (auto* c : std::initializer_list<juce::Component*> { &wave1, &wave2, &table, &loop, &loadBtn }) c->setVisible (false);
        auto grid = [&] (std::initializer_list<juce::Component*> items)
        {
            int i = 0;
            for (auto* c : items) { c->setVisible (true); c->setBounds (8 + (i % 5) * 84, 78 + (i / 5) * 96 + (i >= 5 ? 0 : 0), 84, 96); ++i; }
        };
        auto& timbreKnob = static_cast<vegaui::Knob&> (K ("timbre"));
        switch (eng)
        {
            case VA:
                wave1.setVisible (true); wave2.setVisible (true);
                wave1.setBounds (210, 18, 100, 46); wave2.setBounds (316, 18, 100, 46);
                timbreKnob.label.setText ("Osc Mix", juce::dontSendNotification);
                grid ({ &K ("timbre"), &K ("semi2"), &K ("pw"), &K ("sub"), &K ("noise") });
                break;
            case Wavetable:
                table.setVisible (true); table.setBounds (210, 18, 160, 46);
                timbreKnob.label.setText ("Position", juce::dontSendNotification);
                grid ({ &K ("timbre"), &K ("sub"), &K ("noise") });
                break;
            case FM:
                timbreKnob.label.setText ("FM Index", juce::dontSendNotification);
                grid ({ &K ("timbre"), &K ("fmRatio"), &K ("fmFeedback"), &K ("sub"), &K ("noise") });
                break;
            case Sample:
                loadBtn.setVisible (true); loop.setVisible (true); loop.setBounds (356, 14, 64, 60);
                timbreKnob.label.setText ("Start", juce::dontSendNotification);
                grid ({ &K ("timbre"), &K ("root"), &K ("noise") });
                break;
            default:
                loadBtn.setVisible (true);
                timbreKnob.label.setText ("Position", juce::dontSendNotification);
                grid ({ &K ("timbre"), &K ("grainSize"), &K ("density"), &K ("spray"), &K ("root") });
                break;
        }
    }
    void timerCallback() override
    {
        const int eng = juce::roundToInt (getParam (proc.apvts, L (layer, "engine")));
        if (eng != lastEngine) { lastEngine = eng; layoutEngine(); repaint(); }
        if (eng >= Sample) repaint (200, 60, 230, 20);
    }
    VegaSynthProcessor& proc; int layer; int lastEngine = -1;
    std::map<std::string, std::unique_ptr<vegaui::Knob>> knobs;
    vegaui::Choice engine, wave1, wave2, table; vegaui::Toggle loop;
    vegaui::Choice f1Type, f2Type; vegaui::Toggle routing, output, arpOn;
    EnvView ampEnv, modEnv;
    juce::TextButton loadBtn; std::unique_ptr<juce::FileChooser> chooser; juce::String status;
};

//==============================================================================
class LayerCard : public juce::Component, private juce::Timer
{
public:
    LayerCard (VegaSynthProcessor& p, int l) : proc (p), layer (l), on (p.apvts, L (l, "on"), "")
    {
        addAndMakeVisible (on); startTimerHz (15);
    }
    std::function<void()> onClick;
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool sel = proc.selectedLayer.load() == layer;
        g.setColour (sel ? juce::Colour (0xff1a1a1a) : juce::Colour (0xff222222)); g.fillRoundedRectangle (b, 4.0f);
        g.setColour (sel ? VegaColours::accent : VegaColours::line); g.drawRoundedRectangle (b, 4.0f, sel ? 1.5f : 1.0f);
        g.setColour (VegaColours::text); g.setFont (VegaFonts::ui (15.0f, true));
        g.drawText ("LAYER " + juce::String::charToString ((juce::juce_wchar) ('A' + layer)), 12, 8, 120, 20, juce::Justification::left);
        auto& s = proc.apvts;
        g.setFont (VegaFonts::ui (11.0f)); g.setColour (VegaColours::textDim);
        g.drawText (engineNames()[juce::roundToInt (getParam (s, L (layer, "engine")))], 12, 30, 150, 14, juce::Justification::left);
        const int lo = juce::roundToInt (getParam (s, L (layer, "keyLo"))), hi = juce::roundToInt (getParam (s, L (layer, "keyHi")));
        g.drawText (noteName (lo) + " - " + noteName (hi) + (getParam (s, L (layer, "output")) > 0.5f ? "   OUT " + juce::String::charToString ((juce::juce_wchar) ('A' + layer)) : juce::String())
                    + (getParam (s, L (layer, "arp")) > 0.5f ? "   ARP" : ""), 12, 46, 200, 14, juce::Justification::left);
        // key range bar
        auto kb = juce::Rectangle<float> (12, b.getBottom() - 30, b.getWidth() - 70, 6);
        g.setColour (juce::Colour (0xff151515)); g.fillRect (kb);
        g.setColour (VegaColours::accent.withAlpha (0.7f)); g.fillRect (kb.getX() + kb.getWidth() * lo / 127.0f, kb.getY(), kb.getWidth() * (hi - lo) / 127.0f, kb.getHeight());
        // level meter
        const float pk = level;
        g.setColour (juce::Colour (0xff151515)); g.fillRect (12.0f, b.getBottom() - 18, kb.getWidth(), 5.0f);
        g.setColour (VegaColours::accent); g.fillRect (12.0f, b.getBottom() - 18, kb.getWidth() * juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (pk, -60.0f) / 60.0f), 5.0f);
    }
    void resized() override { on.setBounds (getWidth() - 56, 10, 46, 70); }
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
private:
    void timerCallback() override { const float v = proc.engine.layerPeak[(size_t) layer].exchange (0.0f); level = std::max (v, level * 0.85f); repaint(); }
    VegaSynthProcessor& proc; int layer; vegaui::Toggle on; float level = 0;
};

//==============================================================================
class MsegEditor : public juce::Component, private juce::Timer
{
public:
    MsegEditor (APVTS& s) : st (s) { startTimerHz (20); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); vegaui::drawScreen (g, b);
        auto r = plot();
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int i = 1; i < 8; ++i) g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 8), r.getY(), r.getBottom());
        auto pts = points();
        juce::Path p; p.startNewSubPath (r.getX(), toY (pts[0].second));
        for (auto& [t, l] : pts) p.lineTo (toX (t), toY (l));
        p.lineTo (r.getRight(), toY (pts.back().second));
        g.setColour (VegaColours::accent); g.strokePath (p, juce::PathStrokeType (2.0f));
        for (int i = 0; i < kMsegPoints; ++i)
        {
            const float t = getParam (st, "msegT" + juce::String (i)), l = getParam (st, "msegL" + juce::String (i));
            g.setColour (i == drag ? juce::Colours::white : VegaColours::accent); g.fillEllipse (toX (t) - 5, toY (l) - 5, 10, 10);
        }
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("MSEG  -  drag the points", 8, 4, 220, 14, juce::Justification::left);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = -1; float best = 14;
        for (int i = 0; i < kMsegPoints; ++i)
        {
            const float d = e.position.getDistanceFrom ({ toX (getParam (st, "msegT" + juce::String (i))), toY (getParam (st, "msegL" + juce::String (i))) });
            if (d < best) { best = d; drag = i; }
        }
        if (drag >= 0) for (auto n : { "msegT", "msegL" }) if (auto* p = st.getParameter (n + juce::String (drag))) p->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        auto r = plot();
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()), l = juce::jlimit (0.0f, 1.0f, (r.getBottom() - e.position.y) / r.getHeight());
        st.getParameter ("msegT" + juce::String (drag))->setValueNotifyingHost (t);
        st.getParameter ("msegL" + juce::String (drag))->setValueNotifyingHost (l);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0) for (auto n : { "msegT", "msegL" }) if (auto* p = st.getParameter (n + juce::String (drag))) p->endChangeGesture();
        drag = -1;
    }
private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (12.0f, 22.0f).withTrimmedTop (-4); }
    float toX (float t) const { auto r = plot(); return r.getX() + t * r.getWidth(); }
    float toY (float l) const { auto r = plot(); return r.getBottom() - l * r.getHeight(); }
    std::vector<std::pair<float, float>> points() const
    {
        std::vector<std::pair<float, float>> p;
        for (int i = 0; i < kMsegPoints; ++i) p.push_back ({ getParam (st, "msegT" + juce::String (i)), getParam (st, "msegL" + juce::String (i)) });
        std::sort (p.begin(), p.end()); return p;
    }
    void timerCallback() override { repaint(); }
    APVTS& st; int drag = -1;
};

class SeqEditor : public juce::Component, private juce::Timer
{
public:
    SeqEditor (VegaSynthProcessor& p) : proc (p) { startTimerHz (20); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat(); vegaui::drawScreen (g, b);
        auto r = b.reduced (12.0f, 22.0f);
        const int len = juce::roundToInt (getParam (proc.apvts, "seqLen"));
        const float w = r.getWidth() / kSeqSteps, mid = r.getCentreY();
        g.setColour (juce::Colours::white.withAlpha (0.1f)); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        for (int i = 0; i < kSeqSteps; ++i)
        {
            const float v = getParam (proc.apvts, "seq" + juce::String (i));
            auto bar = juce::Rectangle<float>::leftTopRightBottom (r.getX() + i * w + 2, std::min (mid, mid - v * r.getHeight() * 0.5f), r.getX() + (i + 1) * w - 2, std::max (mid, mid - v * r.getHeight() * 0.5f));
            g.setColour (i < len ? VegaColours::accent.withAlpha (0.8f) : juce::Colour (0xff333333)); g.fillRect (bar);
        }
        const float sv = proc.engine.seqValue.load();
        g.setColour (juce::Colours::white); g.fillEllipse (r.getRight() + 2, mid - sv * r.getHeight() * 0.5f - 3, 6, 6);
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("STEP SEQUENCER  -  draw the steps", 8, 4, 260, 14, juce::Justification::left);
    }
    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat().reduced (12.0f, 22.0f);
        const int i = juce::jlimit (0, kSeqSteps - 1, (int) ((e.position.x - r.getX()) / (r.getWidth() / kSeqSteps)));
        const float v = juce::jlimit (-1.0f, 1.0f, (r.getCentreY() - e.position.y) / (r.getHeight() * 0.5f));
        if (auto* p = proc.apvts.getParameter ("seq" + juce::String (i))) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
private:
    void timerCallback() override { repaint(); }
    VegaSynthProcessor& proc;
};

class MatrixRow : public juce::Component
{
public:
    MatrixRow (APVTS& s, int slot) : src (s, "mod" + juce::String (slot) + "Src", ""), dst (s, "mod" + juce::String (slot) + "Dst", ""), st (s), idx (slot)
    {
        amt.setSliderStyle (juce::Slider::LinearBar); amt.setTextBoxIsEditable (false);
        att = std::make_unique<APVTS::SliderAttachment> (s, "mod" + juce::String (slot) + "Amt", amt);
        clear.setButtonText ("X");
        clear.onClick = [this] { setParam (st, "mod" + juce::String (idx) + "Src", 0); setParam (st, "mod" + juce::String (idx) + "Dst", 0); setParam (st, "mod" + juce::String (idx) + "Amt", 0); };
        for (auto* c : std::initializer_list<juce::Component*> { &src, &dst, &amt, &clear }) addAndMakeVisible (c);
    }
    void resized() override
    {
        src.setBounds (20, -16, 110, 46); dst.setBounds (134, -16, 170, 46);
        amt.setBounds (310, 4, getWidth() - 350, 20); clear.setBounds (getWidth() - 34, 4, 28, 20);
    }
    void paint (juce::Graphics& g) override { g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (10.0f)); g.drawText (juce::String (idx + 1), 0, 0, 18, getHeight(), juce::Justification::centred); }
    vegaui::Choice src, dst; juce::Slider amt; juce::TextButton clear; APVTS& st; int idx;
    std::unique_ptr<APVTS::SliderAttachment> att;
};

class ModPage : public juce::Component
{
public:
    ModPage (VegaSynthProcessor& p) : proc (p), mseg (p.apvts), seq (p)
    {
        for (int i = 0; i < 2; ++i)
        {
            const juce::String n = "lfo" + juce::String (i + 1);
            lfoWave[i] = std::make_unique<vegaui::Choice> (p.apvts, n + "Wave", "Wave");
            lfoRate[i] = std::make_unique<vegaui::Knob> (p.apvts, n + "Rate", "Rate");
            lfoSync[i] = std::make_unique<vegaui::Toggle> (p.apvts, n + "Sync", "Sync");
            lfoBeat[i] = std::make_unique<vegaui::Choice> (p.apvts, n + "Beat", "Sync Rate");
            lfoRetrig[i] = std::make_unique<vegaui::Toggle> (p.apvts, n + "Retrig", "Retrig");
            for (juce::Component* c : { (juce::Component*) lfoWave[i].get(), (juce::Component*) lfoRate[i].get(), (juce::Component*) lfoSync[i].get(),
                                        (juce::Component*) lfoBeat[i].get(), (juce::Component*) lfoRetrig[i].get() }) addAndMakeVisible (c);
        }
        msegLen = std::make_unique<vegaui::Knob> (p.apvts, "msegLen", "Length");
        msegSync = std::make_unique<vegaui::Toggle> (p.apvts, "msegSync", "Sync");
        msegBeat = std::make_unique<vegaui::Choice> (p.apvts, "msegBeat", "Sync Length");
        msegLoop = std::make_unique<vegaui::Toggle> (p.apvts, "msegLoop", "Loop");
        seqLen = std::make_unique<vegaui::Knob> (p.apvts, "seqLen", "Steps");
        seqBeat = std::make_unique<vegaui::Choice> (p.apvts, "seqBeat", "Rate");
        seqGlide = std::make_unique<vegaui::Knob> (p.apvts, "seqGlide", "Smooth");
        for (juce::Component* c : { (juce::Component*) &mseg, (juce::Component*) &seq, (juce::Component*) msegLen.get(), (juce::Component*) msegSync.get(),
                                    (juce::Component*) msegBeat.get(), (juce::Component*) msegLoop.get(), (juce::Component*) seqLen.get(),
                                    (juce::Component*) seqBeat.get(), (juce::Component*) seqGlide.get() }) addAndMakeVisible (c);
        for (int i = 0; i < kModSlots; ++i) { rows[(size_t) i] = std::make_unique<MatrixRow> (p.apvts, i); addAndMakeVisible (*rows[(size_t) i]); }
    }
    void paint (juce::Graphics& g) override
    {
        vegaui::drawSection (g, { 0, 0, 330, getHeight() }, "LFOS", false);
        vegaui::drawSection (g, { 330, 0, 600, getHeight() }, "MSEG  /  STEP SEQUENCER");
        vegaui::drawSection (g, { 930, 0, getWidth() - 930, getHeight() }, "MODULATION MATRIX");
        for (int i = 0; i < 2; ++i)
        {
            g.setFont (VegaFonts::ui (12.0f, true)); g.setColour (VegaColours::text);
            g.drawText ("LFO " + juce::String (i + 1), 12, 22 + i * 280, 80, 16, juce::Justification::left);
            const float v = proc.engine.lfoValue[i].load();
            auto r = juce::Rectangle<float> (12, 230.0f + i * 280, 300, 14);
            g.setColour (juce::Colour (0xff151515)); g.fillRect (r);
            g.setColour (VegaColours::accent); g.fillRect (r.getCentreX() + std::min (0.0f, v) * r.getWidth() * 0.5f, r.getY(), std::abs (v) * r.getWidth() * 0.5f, r.getHeight());
        }
        g.setFont (VegaFonts::ui (10.0f)); g.setColour (VegaColours::textDim);
        g.drawText ("Drag a source chip (top right) onto any knob with a ring, or set routes here. Right-click a knob to edit its amounts.",
                    940, getHeight() - 22, getWidth() - 950, 16, juce::Justification::left);
    }
    void resized() override
    {
        for (int i = 0; i < 2; ++i)
        {
            const int y = 40 + i * 280;
            lfoWave[i]->setBounds (12, y, 130, 46); lfoRate[i]->setBounds (150, y - 6, 90, 110); lfoRetrig[i]->setBounds (246, y, 70, 74);
            lfoSync[i]->setBounds (12, y + 70, 70, 74); lfoBeat[i]->setBounds (90, y + 112, 110, 46);
        }
        mseg.setBounds (340, 20, 470, 250);
        msegLen->setBounds (816, 16, 104, 100); msegLoop->setBounds (816, 116, 50, 74); msegSync->setBounds (868, 116, 50, 74); msegBeat->setBounds (816, 196, 104, 46);
        seq.setBounds (340, 290, 470, 270);
        seqLen->setBounds (816, 290, 104, 100); seqBeat->setBounds (816, 400, 104, 46); seqGlide->setBounds (816, 452, 104, 100);
        for (int i = 0; i < kModSlots; ++i) rows[(size_t) i]->setBounds (936, 22 + i * 32, getWidth() - 942, 30);
    }
    void refresh() { repaint (0, 0, 330, getHeight()); }
private:
    VegaSynthProcessor& proc;
    std::unique_ptr<vegaui::Choice> lfoWave[2], lfoBeat[2]; std::unique_ptr<vegaui::Knob> lfoRate[2]; std::unique_ptr<vegaui::Toggle> lfoSync[2], lfoRetrig[2];
    MsegEditor mseg; SeqEditor seq;
    std::unique_ptr<vegaui::Knob> msegLen, seqLen, seqGlide; std::unique_ptr<vegaui::Toggle> msegSync, msegLoop; std::unique_ptr<vegaui::Choice> msegBeat, seqBeat;
    std::array<std::unique_ptr<MatrixRow>, kModSlots> rows;
};

//==============================================================================
class ArpPage : public juce::Component
{
public:
    ArpPage (VegaSynthProcessor& p) : proc (p),
        on (p.apvts, "arpOn", "Arp On"), mode (p.apvts, "arpMode", "Mode"), rate (p.apvts, "arpRate", "Rate"),
        oct (p.apvts, "arpOct", "Octaves"), gate (p.apvts, "arpGate", "Gate"), swing (p.apvts, "arpSwing", "Swing"),
        prob (p.apvts, "arpProb", "Probability", true), ratchet (p.apvts, "arpRatchet", "Ratchet"), ratchetProb (p.apvts, "arpRatchetProb", "Ratchet Chance"),
        latch (p.apvts, "arpLatch", "Latch"), chord (p.apvts, "chordMem", "Chord Memory")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &rate, &oct, &gate, &swing, &prob, &ratchet, &ratchetProb, &latch, &chord }) addAndMakeVisible (c);
        for (int l = 0; l < kLayers; ++l)
        {
            layerArp[(size_t) l] = std::make_unique<vegaui::Toggle> (p.apvts, L (l, "arp"), "Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + l)));
            addAndMakeVisible (*layerArp[(size_t) l]);
        }
    }
    void paint (juce::Graphics& g) override
    {
        vegaui::drawSection (g, { 0, 0, 900, 300 }, "ARPEGGIATOR", false);
        vegaui::drawSection (g, { 900, 0, getWidth() - 900, 300 }, "LAYERS FOLLOWING THE ARP");
        vegaui::drawSection (g, { 0, 310, 900, getHeight() - 310 }, "CHORD MEMORY", false);
        g.setFont (VegaFonts::ui (12.0f)); g.setColour (VegaColours::textDim);
        g.drawFittedText ("Chord Memory: switch it on, play a chord and let go. From then on every single key plays that chord shape "
                          "(transposed), straight into the layers or into the arpeggiator. Switch it off to clear the chord.\n\n"
                          "Probability sets how often a step plays. Ratchet repeats a step 2-4 times within its length; Ratchet Chance sets how often that happens. "
                          "Swing delays every second step. Latch keeps the pattern running after you let go of the keys.",
                          juce::Rectangle<int> (150, 330, 720, 160), juce::Justification::topLeft, 8);
    }
    void resized() override
    {
        on.setBounds (16, 30, 70, 74); mode.setBounds (100, 34, 140, 46); rate.setBounds (250, 34, 120, 46); oct.setBounds (380, 20, 90, 110);
        gate.setBounds (480, 20, 90, 110); swing.setBounds (580, 20, 90, 110); latch.setBounds (690, 30, 70, 74);
        prob.setBounds (60, 140, 150, 150); ratchet.setBounds (240, 160, 100, 110); ratchetProb.setBounds (350, 160, 110, 110);
        for (int l = 0; l < kLayers; ++l) layerArp[(size_t) l]->setBounds (920 + l * 120, 40, 80, 80);
        chord.setBounds (30, 340, 100, 80);
    }
private:
    VegaSynthProcessor& proc;
    vegaui::Toggle on; vegaui::Choice mode, rate; vegaui::Knob oct, gate, swing, prob, ratchet, ratchetProb; vegaui::Toggle latch, chord;
    std::array<std::unique_ptr<vegaui::Toggle>, kLayers> layerArp;
};

class FxPage : public juce::Component
{
public:
    FxPage (VegaSynthProcessor& p) : proc (p), ping (p.apvts, "dlyPing", "Ping-Pong"), dlyBeat (p.apvts, "dlyBeat", "Time")
    {
        auto k = [&] (const char* id, const char* t) { knobs.push_back (makeKnob (p, id, t)); addAndMakeVisible (*knobs.back()); };
        k ("distDrive", "Drive"); k ("distMix", "Mix");
        k ("choRate", "Rate"); k ("choDepth", "Depth"); k ("choMix", "Mix");
        k ("dlyFeedback", "Feedback"); k ("dlyTone", "Tone"); k ("dlyMix", "Return");
        k ("revSize", "Size"); k ("revDamp", "Damping"); k ("revWidth", "Width"); k ("revMix", "Return");
        k ("eqLow", "Low"); k ("eqMid", "Mid"); k ("eqHigh", "High");
        addAndMakeVisible (ping); addAndMakeVisible (dlyBeat);
    }
    void paint (juce::Graphics& g) override
    {
        const char* t[] { "DISTORTION", "CHORUS", "DELAY (SENDS)", "REVERB (SENDS)", "MASTER EQ" };
        for (int i = 0; i < 5; ++i) vegaui::drawSection (g, { x[i], 0, x[i + 1] - x[i], 300 }, t[i], i > 0);
        g.setFont (VegaFonts::ui (12.0f)); g.setColour (VegaColours::textDim);
        g.drawFittedText ("Each layer has its own Drive, Chorus, EQ and Delay/Reverb sends (LAYERS page). The master chain runs Distortion -> Chorus -> EQ, "
                          "then adds the Delay and Reverb returns. Layers set to Output: Separate skip the master chain and go to their own DAW output "
                          "(enable the Vega Multisynth 'Layer A-D' outputs in your DAW's plugin routing).",
                          juce::Rectangle<int> (20, 330, getWidth() - 40, 80), juce::Justification::topLeft, 4);
    }
    void resized() override
    {
        x = { 0, 220, 520, 860, 1220, getWidth() };
        auto put = [&] (int idx, int px, int py) { knobs[(size_t) idx]->setBounds (px, py, 95, 115); };
        put (0, 15, 30); put (1, 115, 30);
        put (2, 230, 30); put (3, 325, 30); put (4, 420, 30);
        dlyBeat.setBounds (530, 40, 110, 46); ping.setBounds (650, 30, 80, 74);
        put (5, 530, 150); put (6, 630, 150); put (7, 740, 30);
        put (8, 870, 30); put (9, 960, 30); put (10, 870, 150); put (11, 1100, 30);
        put (12, 1230, 30); put (13, 1325, 30); put (14, 1230, 150);
    }
private:
    VegaSynthProcessor& proc; std::vector<std::unique_ptr<vegaui::Knob>> knobs; vegaui::Toggle ping; vegaui::Choice dlyBeat; std::array<int, 6> x {};
};

//==============================================================================
class BrowserPage : public juce::Component, private juce::ListBoxModel
{
public:
    BrowserPage (VegaSynthProcessor& p) : proc (p)
    {
        search.setTextToShowWhenEmpty ("Search presets...", VegaColours::textDim);
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff151515));
        search.setColour (juce::TextEditor::textColourId, VegaColours::text);
        search.setColour (juce::TextEditor::outlineColourId, VegaColours::line);
        search.setColour (juce::TextEditor::focusedOutlineColourId, VegaColours::accent);
        search.onTextChange = [this] { rebuild(); };
        addAndMakeVisible (search);
        auto tagBtn = [&] (const juce::String& t)
        {
            auto b = std::make_unique<juce::TextButton> (t); b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, VegaColours::accent);
            b->onClick = [this] { rebuild(); }; addAndMakeVisible (*b); return b;
        };
        for (auto& t : presetTypes()) typeBtns.push_back (tagBtn (t));
        for (auto& t : presetGenres()) genreBtns.push_back (tagBtn (t));
        favBtn = tagBtn (juce::String::fromUTF8 ("\xe2\x98\x85 Favorites"));
        preview.setButtonText ("Audition on click"); preview.setToggleState (true, juce::dontSendNotification); addAndMakeVisible (preview);
        save.setButtonText ("SAVE PRESET..."); save.onClick = [this] { saveDialog(); }; addAndMakeVisible (save);
        list.setModel (this); list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff161616));
        addAndMakeVisible (list);
        rebuild();
    }
    void rebuild()
    {
        all = proc.allPresets(); shown.clear();
        const auto q = search.getText().toLowerCase();
        juce::StringArray types, genres;
        for (auto& b : typeBtns) if (b->getToggleState()) types.add (b->getButtonText());
        for (auto& b : genreBtns) if (b->getToggleState()) genres.add (b->getButtonText());
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& p = all[(size_t) i];
            if (q.isNotEmpty() && ! (p.name.toLowerCase().contains (q) || p.type.toLowerCase().contains (q) || p.genre.toLowerCase().contains (q))) continue;
            if (! types.isEmpty() && ! types.contains (p.type)) continue;
            if (! genres.isEmpty() && ! genres.contains (p.genre)) continue;
            if (favBtn->getToggleState() && ! proc.isFavorite (p.name)) continue;
            shown.push_back (i);
        }
        list.updateContent(); list.repaint();
    }
    void paint (juce::Graphics& g) override
    {
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("TYPE", 10, 50, 60, 16, juce::Justification::left);
        g.drawText ("GENRE", 10, 82, 60, 16, juce::Justification::left);
        g.drawText (juce::String ((int) shown.size()) + " presets   -   click to audition, double-click or Enter to load, click the star to favourite",
                    10, getHeight() - 20, 700, 16, juce::Justification::left);
    }
    void resized() override
    {
        search.setBounds (10, 10, 360, 26); favBtn->setBounds (380, 10, 110, 26); preview.setBounds (510, 10, 180, 26); save.setBounds (getWidth() - 150, 10, 140, 26);
        int x = 70; for (auto& b : typeBtns) { b->setBounds (x, 46, 88, 24); x += 92; }
        x = 70; for (auto& b : genreBtns) { b->setBounds (x, 78, 120, 24); x += 124; }
        list.setBounds (10, 112, getWidth() - 20, getHeight() - 140);
    }
private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& p = all[(size_t) shown[(size_t) row]];
        if (selected) { g.setColour (VegaColours::accentDim); g.fillRect (0, 0, w, h); }
        else if (row % 2) { g.setColour (juce::Colours::white.withAlpha (0.02f)); g.fillRect (0, 0, w, h); }
        g.setColour (proc.isFavorite (p.name) ? VegaColours::gold : VegaColours::textDim.withAlpha (0.5f));
        g.setFont (VegaFonts::ui (15.0f)); g.drawText (juce::String::fromUTF8 (proc.isFavorite (p.name) ? "\xe2\x98\x85" : "\xe2\x98\x86"), 6, 0, 24, h, juce::Justification::centred);
        g.setColour (p.name == proc.currentPresetName ? VegaColours::accent : VegaColours::text); g.setFont (VegaFonts::ui (13.0f, p.name == proc.currentPresetName));
        g.drawText (p.name, 36, 0, 360, h, juce::Justification::centredLeft);
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (11.5f));
        g.drawText (p.type, 420, 0, 140, h, juce::Justification::centredLeft);
        g.drawText (p.genre, 570, 0, 180, h, juce::Justification::centredLeft);
        g.drawText (p.factory ? "Factory" : "User", 780, 0, 100, h, juce::Justification::centredLeft);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& p = all[(size_t) shown[(size_t) row]];
        if (e.x < 32) { proc.toggleFavorite (p.name); list.repaintRow (row); return; }
        if (preview.getToggleState()) { proc.loadPreset (p); proc.previewNote (60); list.repaint(); }
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { returnKeyPressed (row); }
    void returnKeyPressed (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        proc.loadPreset (all[(size_t) shown[(size_t) row]]); list.repaint();
    }
    void saveDialog()
    {
        auto* w = new juce::AlertWindow ("Save preset", "Name and tags for this sound:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", proc.currentPresetName, "Name");
        w->addComboBox ("type", presetTypes(), "Type"); w->addComboBox ("genre", presetGenres(), "Genre");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r == 1)
                proc.saveUserPreset (w->getTextEditorContents ("name"), w->getComboBoxComponent ("type")->getText(), w->getComboBoxComponent ("genre")->getText());
            rebuild();
        }), true);
    }
    VegaSynthProcessor& proc;
    juce::TextEditor search; std::vector<std::unique_ptr<juce::TextButton>> typeBtns, genreBtns; std::unique_ptr<juce::TextButton> favBtn;
    juce::ToggleButton preview; juce::TextButton save; juce::ListBox list;
    std::vector<Preset> all; std::vector<int> shown;
};
} // namespace

//==============================================================================
class VegaSynthEditor::Main : public juce::Component
{
public:
    Main (VegaSynthProcessor& p) : proc (p), mods (p), arpPage (p), fxPage (p), browser (p),
        keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        for (int l = 0; l < kLayers; ++l)
        {
            cards[(size_t) l] = std::make_unique<LayerCard> (p, l);
            cards[(size_t) l]->onClick = [this, l] { selectLayer (l); };
            addAndMakeVisible (*cards[(size_t) l]);
            layers[(size_t) l] = std::make_unique<LayerPanel> (p, l);
            addChildComponent (*layers[(size_t) l]);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &mods, &arpPage, &fxPage, &browser }) addChildComponent (c);
        const char* tn[] { "LAYERS", "MOD", "ARP", "FX", "BROWSER" };
        for (int i = 0; i < 5; ++i)
        {
            tabs[(size_t) i].setButtonText (tn[i]); tabs[(size_t) i].setClickingTogglesState (true); tabs[(size_t) i].setRadioGroupId (9);
            tabs[(size_t) i].onClick = [this, i] { showTab (i); };
            addAndMakeVisible (tabs[(size_t) i]);
        }
        for (int s = 1; s < NumSources; ++s) { chips.push_back (std::make_unique<SourceChip> (s)); addAndMakeVisible (*chips.back()); }
        for (int m = 0; m < kMacros; ++m) { macros[(size_t) m] = std::make_unique<vegaui::Knob> (p.apvts, "macro" + juce::String (m + 1), "Macro " + juce::String (m + 1)); addAndMakeVisible (*macros[(size_t) m]); }
        master = makeKnob (p, "master", "Master"); addAndMakeVisible (*master);
        addAndMakeVisible (meter);
        keyboard.setAvailableRange (24, 108); keyboard.setLowestVisibleKey (36); keyboard.setKeyWidth (22.0f);
        keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, VegaColours::accent.withAlpha (0.7f));
        addAndMakeVisible (keyboard);
        prev.setButtonText ("<"); next.setButtonText (">"); browse.setButtonText ("BROWSE");
        prev.onClick = [this] { step (-1); }; next.onClick = [this] { step (1); };
        browse.onClick = [this] { showTab (4); };
        for (auto* b : { &prev, &next, &browse }) addAndMakeVisible (b);
        presetLabel.setJustificationType (juce::Justification::centred);
        presetLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff3a3a3a));
        presetLabel.setFont (VegaFonts::ui (12.0f));
        addAndMakeVisible (presetLabel);
        selectLayer (proc.selectedLayer.load());
        showTab (0);
    }

    void selectLayer (int l)
    {
        proc.selectedLayer.store (l);
        for (int i = 0; i < kLayers; ++i) layers[(size_t) i]->setVisible (i == l && tab == 0);
        for (auto& c : cards) c->repaint();
    }
    void showTab (int t)
    {
        tab = t;
        for (int i = 0; i < 5; ++i) tabs[(size_t) i].setToggleState (i == t, juce::dontSendNotification);
        for (auto& c : cards) c->setVisible (t == 0);
        for (int i = 0; i < kLayers; ++i) layers[(size_t) i]->setVisible (t == 0 && i == proc.selectedLayer.load());
        mods.setVisible (t == 1); arpPage.setVisible (t == 2); fxPage.setVisible (t == 3); browser.setVisible (t == 4);
        if (t == 4) browser.rebuild();
        repaint();
    }
    void step (int d)
    {
        auto all = proc.allPresets(); int idx = 0;
        for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].name == proc.currentPresetName) idx = i;
        idx = (idx + d + (int) all.size()) % (int) all.size();
        proc.loadPreset (all[(size_t) idx]);
    }
    void refresh()
    {
        meter.setLevels (proc.outPeakL.exchange (0.0f), proc.outPeakR.exchange (0.0f));
        if (presetLabel.getText() != proc.currentPresetName) presetLabel.setText (proc.currentPresetName, juce::dontSendNotification);
        if (tab == 1) mods.refresh();
        const int v = proc.engine.activeVoices.load();
        if (v != lastVoices) { lastVoices = v; repaint (W - 240, 0, 120, 34); }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "MULTISYNTH  VP-7");
        vegaui::drawBody (g, { 0, 35, W, 620 });
        g.setColour (VegaColours::line); g.drawHorizontalLine (72, 0, (float) W);
        g.setFont (VegaFonts::ui (10.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("MOD SOURCES", W - 760, 44, 90, 20, juce::Justification::centredRight);
        g.drawText ("VOICES " + juce::String (lastVoices), W - 240, 0, 110, 34, juce::Justification::centredRight);
        // bottom performance strip
        g.setColour (juce::Colour (0xff262626)); g.fillRect (0, 656, W, 194);
        g.setColour (VegaColours::line); g.drawHorizontalLine (656, 0, (float) W);
        vegaui::drawSection (g, { 0, 660, 470, 186 }, "MACROS", false);
        vegaui::drawSection (g, { 470, 660, 200, 186 }, "MASTER");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "VA  /  WAVETABLE  /  FM  /  SAMPLE  /  GRANULAR   -   4 LAYERS   -   DRAG-AND-DROP MODULATION");
    }
    void resized() override
    {
        presetLabel.setBounds (W / 2 - 150, 6, 240, 22);
        prev.setBounds (W / 2 + 94, 6, 22, 22); next.setBounds (W / 2 + 118, 6, 22, 22); browse.setBounds (W / 2 + 146, 6, 70, 22);
        for (int i = 0; i < 5; ++i) tabs[(size_t) i].setBounds (10 + i * 94, 42, 90, 24);
        int cx = W - 664;
        for (auto& c : chips) { c->setBounds (cx, 43, 48, 22); cx += 50; }
        const juce::Rectangle<int> page (10, 78, W - 20, 574);
        for (int l = 0; l < kLayers; ++l) cards[(size_t) l]->setBounds (10, 80 + l * 142, 220, 138);
        for (auto& lp : layers) lp->setBounds (240, 80, W - 250, 570);
        for (juce::Component* c : { (juce::Component*) &mods, (juce::Component*) &arpPage, (juce::Component*) &fxPage, (juce::Component*) &browser }) c->setBounds (page);
        for (int m = 0; m < kMacros; ++m) macros[(size_t) m]->setBounds (14 + m * 112, 680, 108, 150);
        master->setBounds (500, 680, 110, 150);
        meter.setBounds (614, 676, 52, 160);
        keyboard.setBounds (680, 676, W - 690, 160);
    }
private:
    VegaSynthProcessor& proc; int tab = 0, lastVoices = 0;
    std::array<std::unique_ptr<LayerCard>, kLayers> cards; std::array<std::unique_ptr<LayerPanel>, kLayers> layers;
    ModPage mods; ArpPage arpPage; FxPage fxPage; BrowserPage browser;
    std::array<juce::TextButton, 5> tabs; std::vector<std::unique_ptr<SourceChip>> chips;
    std::array<std::unique_ptr<vegaui::Knob>, kMacros> macros; std::unique_ptr<vegaui::Knob> master;
    vegaui::StereoMeter meter; juce::MidiKeyboardComponent keyboard;
    juce::TextButton prev, next, browse; juce::Label presetLabel;
};

//==============================================================================
VegaSynthEditor::VegaSynthEditor (VegaSynthProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load() == 1.0f ? 0.85f : proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaSynthEditor::~VegaSynthEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaSynthEditor::timerCallback() { main->refresh(); }
void VegaSynthEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}

juce::AudioProcessorEditor* VegaSynthProcessor::createEditor() { return new VegaSynthEditor (*this); }
