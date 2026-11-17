// Headless test: every factory preset and every engine must make sound, stay finite and below clipping.
#include "../Source/PluginProcessor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto& p = dynamic_cast<VegaSynthProcessor&> (*base);
    const double fs = 48000; const int blk = 256;
    p.setPlayConfigDetails (0, 2, fs, blk); p.prepareToPlay (fs, blk);
    int fails = 0;

    auto run = [&] (const juce::String& label)
    {
        juce::AudioBuffer<float> buf (2, blk); float peak = 0; bool bad = false; double energy = 0;
        for (int b = 0; b < (int) (2.5 * fs / blk); ++b)
        {
            juce::MidiBuffer m;
            if (b == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 90), 3);
                          m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 7); m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80), 9); }
            if (b == (int) (1.6 * fs / blk)) for (int n : { 48, 55, 60, 64 }) m.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            p.processBlock (buf, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < blk; ++i)
            { const float v = buf.getSample (c, i); if (! std::isfinite (v)) bad = true; peak = std::max (peak, std::abs (v)); energy += v * v; }
        }
        const bool ok = ! bad && energy > 1.0e-3 && peak < 4.0f;
        std::cout << (ok ? "PASS " : "FAIL ") << label << "  peak " << juce::Decibels::gainToDecibels (peak) << " dBFS  voices left " << p.engine.activeVoices.load() << "\n";
        if (! ok) ++fails;
    };

    for (auto& pr : p.allPresets()) { p.loadPreset (pr); run ("preset: " + pr.name); }
    p.loadPreset (vms::factoryPresets()[0]);
    for (int e = 0; e < 5; ++e)
    {
        auto* prm = p.apvts.getParameter ("A_engine"); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) e));
        auto* u = p.apvts.getParameter ("A_unison"); u->setValueNotifyingHost (u->convertTo0to1 (5.0f));
        run ("engine: " + vms::engineNames()[e]);
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << std::endl;
    return fails;
}
