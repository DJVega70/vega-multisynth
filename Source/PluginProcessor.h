#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "SynthEngine.h"
#include "Presets.h"

class VegaSynthProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VegaSynthProcessor();
    ~VegaSynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Multisynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return currentPresetName; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    vms::SynthEngine engine;
    juce::MidiKeyboardState keyboardState;
    std::atomic<float> outPeakL { 0 }, outPeakR { 0 };
    std::atomic<float> uiScale { 1.0f };
    std::atomic<int> selectedLayer { 0 };

    // ---- samples
    bool loadSample (int layer, const juce::File& file, juce::String& error);
    juce::String sampleName (int layer) const;

    // ---- presets
    std::vector<vms::Preset> allPresets() const;
    void loadPreset (const vms::Preset& p);
    bool saveUserPreset (const juce::String& name, const juce::String& type, const juce::String& genre);
    static juce::File userPresetFolder();
    bool isFavorite (const juce::String& name) const { return favorites.contains (name); }
    void toggleFavorite (const juce::String& name);
    void previewNote (int note = 60);
    juce::String currentPresetName { "Init" };

private:
    static BusesProperties makeBuses()
    {
        auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
        for (int i = 0; i < vms::kLayers; ++i)
            b = b.withOutput ("Layer " + juce::String::charToString ((juce::juce_wchar) ('A' + i)), juce::AudioChannelSet::stereo(), false);
        return b;
    }
    void timerCallback() override;
    void loadFavorites(); void saveFavorites();
    juce::StringArray favorites;
    std::array<juce::String, vms::kLayers> samplePaths;
    juce::AudioFormatManager formats;
    int previewCountdown = 0, previewNoteNum = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaSynthProcessor)
};
