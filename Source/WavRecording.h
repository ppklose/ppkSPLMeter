#pragma once
#include <JuceHeader.h>
#include "SensorConfig.h"

//==============================================================================
// WAV recording (vibration spec §7.5).
//
// Without accelerometer roles the recording is unchanged from 3.5.2: inputs 1 + 2
// (input 1 twice on mono devices). With an accelerometer it records every input
// that is not Off, up to 8. Each track's role and calibration go into the WAV as
// JSON in the iXML/ASWG "userData" field, so file playback can re-analyse the
// recording with the right roles and calibration.
namespace WavRecording
{
    static constexpr int kMaxTracks = 8;

    struct Track
    {
        int          input  = 0;                 // source channel (live input or file channel)
        SensorRole   role   = SensorRole::Mic;
        double       calDb  = 140.0;             // accelerometer roles only
        juce::String name;
    };

    // Inputs to record for the given roles
    inline std::vector<int> layoutFor (const SensorConfig::Roles& roles, int numInputs)
    {
        numInputs = juce::jmin (numInputs, SensorConfig::kMaxInputs);
        bool anyAccel = false;
        for (int ch = 0; ch < numInputs; ++ch)
            anyAccel = anyAccel || SensorConfig::isAccel (roles[(size_t) ch]);
        if (! anyAccel)
            return { 0, 1 };

        std::vector<int> inputs;
        for (int ch = 0; ch < numInputs && (int) inputs.size() < kMaxTracks; ++ch)
            if (roles[(size_t) ch] != SensorRole::Off)
                inputs.push_back (ch);
        return inputs;
    }

    //==========================================================================
    // Circular buffer of the recorded tracks. Built on the message thread,
    // written by the audio thread; the message thread reads a snapshot
    // (reading across the write is a benign race for audio data).
    class Recorder
    {
    public:
        Recorder (std::vector<int> inputs, int numSamples)
            : inputs_ (std::move (inputs)), size_ (juce::jmax (1, numSamples))
        {
            data_.resize (inputs_.size());
            for (auto& d : data_)
                d.assign ((size_t) size_, 0.0f);
        }

        const std::vector<int>& inputs() const noexcept { return inputs_; }
        int  size() const noexcept                      { return size_; }

        // Audio thread. A missing source channel records input 1 for the
        // second default track (mono device), silence otherwise.
        void push (const juce::AudioBuffer<float>& src, int numSamples) noexcept
        {
            const int nSrc = src.getNumChannels();
            const float* in[kMaxTracks] {};
            for (size_t t = 0; t < inputs_.size(); ++t)
            {
                const int ch = inputs_[t];
                in[t] = ch < nSrc ? src.getReadPointer (ch)
                                  : (t == 1 && inputs_.size() == 2 && nSrc > 0 ? src.getReadPointer (0) : nullptr);
            }
            int pos = writePos_.load (std::memory_order_relaxed);
            for (int s = 0; s < numSamples; ++s)
            {
                for (size_t t = 0; t < inputs_.size(); ++t)
                    data_[t][(size_t) pos] = in[t] != nullptr ? in[t][s] : 0.0f;
                if (++pos >= size_) pos = 0;
            }
            writePos_.store (pos, std::memory_order_release);
        }

        // Message thread: the most recent numSamples (≤ size) as a buffer
        juce::AudioBuffer<float> snapshot (int numSamples) const
        {
            numSamples = juce::jlimit (0, size_, numSamples);
            juce::AudioBuffer<float> out ((int) inputs_.size(), numSamples);
            const int writePos = writePos_.load (std::memory_order_acquire);
            for (size_t t = 0; t < inputs_.size(); ++t)
            {
                auto* dst = out.getWritePointer ((int) t);
                for (int s = 0; s < numSamples; ++s)
                    dst[s] = data_[t][(size_t) ((writePos - numSamples + s + size_) % size_)];
            }
            return out;
        }

    private:
        std::vector<int>                inputs_;
        std::vector<std::vector<float>> data_;
        int                             size_;
        std::atomic<int>                writePos_ { 0 };
    };

    //==========================================================================
    // Track metadata ↔ JSON (stored in WavAudioFormat::aswgUserData)
    inline juce::String toJson (const std::vector<Track>& tracks)
    {
        juce::Array<juce::var> list;
        for (const auto& t : tracks)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("input", t.input);
            o->setProperty ("name",  t.name);
            o->setProperty ("role",  (int) t.role);
            if (SensorConfig::isAccel (t.role))
                o->setProperty ("calDb", t.calDb);
            list.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("app", "SPLMeter");
        root->setProperty ("format", 1);
        root->setProperty ("tracks", list);
        return juce::JSON::toString (juce::var (root), true);
    }

    // Returns false when the text is not SPLMeter track metadata
    inline bool fromJson (const juce::String& text, std::vector<Track>& tracks)
    {
        const auto root = juce::JSON::parse (text);
        if (root.getProperty ("app", {}).toString() != "SPLMeter")
            return false;
        auto* list = root.getProperty ("tracks", {}).getArray();
        if (list == nullptr)
            return false;
        tracks.clear();
        for (const auto& v : *list)
        {
            Track t;
            t.input = v.getProperty ("input", 0);
            t.name  = v.getProperty ("name", {}).toString();
            t.role  = SensorConfig::fromInt (v.getProperty ("role", 0));
            t.calDb = v.getProperty ("calDb", 140.0);
            tracks.push_back (t);
        }
        return true;
    }
}
