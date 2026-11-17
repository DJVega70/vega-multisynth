#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace vms;

VegaSynthProcessor::VegaSynthProcessor()
    : AudioProcessor (makeBuses()), apvts (*this, nullptr, "VegaMultisynth", createLayout())
{
    engine.init (apvts);
    formats.registerBasicFormats();
    loadFavorites();
    startTimerHz (20);
}
VegaSynthProcessor::~VegaSynthProcessor() { stopTimer(); }

bool VegaSynthProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (! l.inputBuses.isEmpty() && ! l.getMainInputChannelSet().isDisabled()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i)
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    return true;
}

void VegaSynthProcessor::prepareToPlay (double sr, int block)
{
    engine.prepare (sr, std::max (32, block));
    keyboardState.reset();
}

void VegaSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    double bpm = 120.0, ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }

    auto main = getBusBuffer (buffer, false, 0);
    std::array<std::pair<float*, float*>, kLayers> outs {};
    for (int i = 0; i < kLayers; ++i)
        if (i + 1 < getBusCount (false) && getBus (false, i + 1)->isEnabled())
        {
            auto b = getBusBuffer (buffer, false, i + 1);
            if (b.getNumChannels() >= 2) outs[(size_t) i] = { b.getWritePointer (0), b.getWritePointer (1) };
        }
    engine.render (midi, main.getWritePointer (0), main.getWritePointer (1), n, outs, bpm, playing, ppq);
    midi.clear();

    auto amax = [] (std::atomic<float>& a, float v) { float prev = a.load(); while (prev < v && ! a.compare_exchange_weak (prev, v)) {} };
    amax (outPeakL, main.getMagnitude (0, 0, n)); amax (outPeakR, main.getMagnitude (1, 0, n));
}

// ---------------------------------------------------------------- samples
bool VegaSynthProcessor::loadSample (int layer, const juce::File& file, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (file));
    if (! r) { error = "Can't read " + file.getFileName(); return false; }
    const auto maxLen = (juce::int64) (r->sampleRate * 60.0);   // samples are held in RAM (up to 60 s)
    const int len = (int) std::min (r->lengthInSamples, maxLen);
    auto s = std::make_unique<SampleData>();
    s->audio.setSize (2, len);
    r->read (&s->audio, 0, len, 0, true, true);
    if (r->numChannels == 1) s->audio.copyFrom (1, 0, s->audio, 0, 0, len);
    s->rate = r->sampleRate; s->name = file.getFileNameWithoutExtension(); s->path = file.getFullPathName();
    samplePaths[(size_t) layer] = file.getFullPathName();
    engine.setSample (layer, std::move (s));
    if (r->lengthInSamples > maxLen) error = "Trimmed to 60 seconds.";
    return true;
}
juce::String VegaSynthProcessor::sampleName (int layer) const
{
    auto* s = engine.getSample (layer); return s ? s->name : juce::String ("(built-in)");
}

// ---------------------------------------------------------------- presets
juce::File VegaSynthProcessor::userPresetFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Vega Productions").getChildFile ("Multisynth Presets");
    d.createDirectory(); return d;
}

std::vector<Preset> VegaSynthProcessor::allPresets() const
{
    auto list = factoryPresets();
    for (auto& f : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vegasynth"))
        if (auto x = juce::XmlDocument::parse (f))
        {
            Preset p; p.factory = false; p.file = f; p.name = f.getFileNameWithoutExtension();
            p.type = x->getStringAttribute ("vegaType", "Keys"); p.genre = x->getStringAttribute ("vegaGenre", "Pop");
            list.push_back (p);
        }
    return list;
}

void VegaSynthProcessor::loadPreset (const Preset& p)
{
    engine.requestKill();
    if (! p.factory)
    {
        if (auto x = juce::XmlDocument::parse (p.file))
        {
            auto t = juce::ValueTree::fromXml (*x);
            for (int i = 0; i < t.getNumChildren(); ++i)
            {
                auto c = t.getChild (i);
                if (auto* prm = apvts.getParameter (c.getProperty ("id").toString()))
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (double) c.getProperty ("value")));
            }
            for (int l = 0; l < kLayers; ++l)
            {
                const auto path = t.getProperty ("sample" + juce::String (l)).toString();
                juce::String err; if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (l, juce::File (path), err);
            }
        }
    }
    else
    {
        for (auto* prm : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) r->setValueNotifyingHost (r->getDefaultValue());
        for (auto& [id, v] : p.values)
            if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
    currentPresetName = p.name;
}

bool VegaSynthProcessor::saveUserPreset (const juce::String& name, const juce::String& type, const juce::String& genre)
{
    auto st = apvts.copyState();
    st.setProperty ("vegaType", type, nullptr); st.setProperty ("vegaGenre", genre, nullptr);
    for (int l = 0; l < kLayers; ++l) st.setProperty ("sample" + juce::String (l), samplePaths[(size_t) l], nullptr);
    auto x = st.createXml();
    if (! x) return false;
    x->setAttribute ("vegaType", type); x->setAttribute ("vegaGenre", genre);
    const auto f = userPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".vegasynth");
    if (! x->writeTo (f)) return false;
    currentPresetName = name;
    return true;
}

void VegaSynthProcessor::loadFavorites()
{
    favorites.clear();
    const auto f = userPresetFolder().getChildFile ("favorites.txt");
    if (f.existsAsFile()) favorites.addLines (f.loadFileAsString());
    favorites.removeEmptyStrings();
}
void VegaSynthProcessor::saveFavorites() { userPresetFolder().getChildFile ("favorites.txt").replaceWithText (favorites.joinIntoString ("\n")); }
void VegaSynthProcessor::toggleFavorite (const juce::String& name)
{
    if (favorites.contains (name)) favorites.removeString (name); else favorites.add (name);
    saveFavorites();
}

void VegaSynthProcessor::previewNote (int note)
{
    if (previewNoteNum >= 0) keyboardState.noteOff (1, previewNoteNum, 0.0f);
    previewNoteNum = note; previewCountdown = 18;   // ~0.9 s at 20 Hz
    keyboardState.noteOn (1, note, 0.8f);
}
void VegaSynthProcessor::timerCallback()
{
    if (previewCountdown > 0 && --previewCountdown == 0 && previewNoteNum >= 0)
    {
        keyboardState.noteOff (1, previewNoteNum, 0.0f); previewNoteNum = -1;
    }
}

// ---------------------------------------------------------------- state
void VegaSynthProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto st = apvts.copyState();
    st.setProperty ("presetName", currentPresetName, nullptr);
    st.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    for (int l = 0; l < kLayers; ++l) st.setProperty ("sample" + juce::String (l), samplePaths[(size_t) l], nullptr);
    if (auto x = st.createXml()) copyXmlToBinary (*x, d);
}
void VegaSynthProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
        if (x->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*x);
            currentPresetName = t.getProperty ("presetName", "Init").toString();
            uiScale.store ((float) (double) t.getProperty ("uiScale", 1.0));
            for (int l = 0; l < kLayers; ++l)
            {
                const auto path = t.getProperty ("sample" + juce::String (l)).toString();
                juce::String err; if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadSample (l, juce::File (path), err);
            }
            apvts.replaceState (t);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaSynthProcessor(); }
