#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaSynthEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit VegaSynthEditor (VegaSynthProcessor&);
    ~VegaSynthEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1500, H = 880;
    class Main;
private:
    void timerCallback() override;
    VegaSynthProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
