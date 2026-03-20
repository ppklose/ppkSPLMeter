#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <deque>
#include <functional>

//==============================================================================
struct SoundEvent
{
    juce::int64  timestampMs = 0;
    juce::String label;
    float        confidence  = 0.0f;
};

//==============================================================================
// Apple-platform bridge — declared here, defined in SoundDetectiveApple.mm
#if JUCE_MAC || JUCE_IOS
namespace SoundDetectiveBridge
{
    void* create   (double sampleRate, float threshold,
                    std::function<void(const juce::String&, float)> onEvent);
    void  destroy  (void* handle) noexcept;
    void  process  (void* handle, const float* data, int numSamples,
                    int64_t framePos) noexcept;
    void  setThreshold (void* handle, float threshold) noexcept;
}
#endif

//==============================================================================
class SoundDetective : private juce::Thread
{
public:
    SoundDetective() : juce::Thread ("SoundDetective") {}

    ~SoundDetective() override
    {
        setEnabled (false);
    }

    //==========================================================================
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;
        hopSamples_ = std::max (1, (int)(sampleRate * 0.25));   // 250 ms hop
        winSamples_ = std::max (1, (int)(sampleRate * 0.5));    // 500 ms window
        writePos_.store (0, std::memory_order_relaxed);
        readPos_.store  (0, std::memory_order_relaxed);
        ringBuf_.fill   (0.0f);
        framePos_ = 0;

        if (enabled_)
        {
            setEnabled (false);
            setEnabled (true);   // restart with new sample rate
        }
    }

    //==========================================================================
    void setEnabled (bool e)
    {
        if (enabled_ == e) return;
        enabled_ = e;

        if (e)
        {
#if JUCE_MAC || JUCE_IOS
            if (appleHandle_)
            {
                SoundDetectiveBridge::destroy (appleHandle_);
                appleHandle_ = nullptr;
            }
            if (sampleRate_ > 0)
            {
                appleHandle_ = SoundDetectiveBridge::create (
                    sampleRate_, threshold_,
                    [this] (const juce::String& label, float conf)
                    {
                        postEvent (label, conf);
                    });
            }
#else
            hState_ = {};
#endif
            startThread();
        }
        else
        {
            stopThread (3000);
#if JUCE_MAC || JUCE_IOS
            if (appleHandle_)
            {
                SoundDetectiveBridge::destroy (appleHandle_);
                appleHandle_ = nullptr;
            }
#endif
        }
    }

    bool  isEnabled()    const noexcept { return enabled_; }
    float getThreshold() const noexcept { return threshold_; }

    void setThreshold (float t) noexcept
    {
        threshold_ = t;
#if JUCE_MAC || JUCE_IOS
        if (appleHandle_) SoundDetectiveBridge::setThreshold (appleHandle_, t);
#endif
    }

    //==========================================================================
    // Called on the audio thread — lock-free ring buffer write
    void pushSamples (const float* data, int numSamples) noexcept
    {
        if (!enabled_) return;
        const unsigned sz = (unsigned) ringBuf_.size();
        unsigned wp = writePos_.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
            ringBuf_[(wp + (unsigned)i) % sz] = data[i];
        writePos_.store (wp + (unsigned)numSamples, std::memory_order_release);
    }

    //==========================================================================
    // Called from the GUI thread — returns and clears pending events
    std::vector<SoundEvent> popNewEvents()
    {
        juce::SpinLock::ScopedLockType lock (eventsLock_);
        std::vector<SoundEvent> out (pendingEvents_.begin(), pendingEvents_.end());
        pendingEvents_.clear();
        return out;
    }

    void clearPendingEvents()
    {
        juce::SpinLock::ScopedLockType lock (eventsLock_);
        pendingEvents_.clear();
    }

private:
    //==========================================================================
    void run() override
    {
        std::vector<float> window (winSamples_, 0.0f);

        while (!threadShouldExit())
        {
            const unsigned wp  = writePos_.load (std::memory_order_acquire);
            const unsigned rp  = readPos_.load  (std::memory_order_relaxed);
            const unsigned avail = wp - rp;   // unsigned subtraction handles wraparound

            if (avail < (unsigned) hopSamples_)
            {
                juce::Thread::sleep (15);
                continue;
            }

            // If we've fallen very far behind, skip ahead to stay near-realtime
            if (avail > (unsigned)(ringBuf_.size() / 2))
            {
                readPos_.store (wp - (unsigned) hopSamples_, std::memory_order_relaxed);
                continue;
            }

            // Build analysis window: winSamples_ ending at rp + hopSamples_
            const unsigned end = rp + (unsigned) hopSamples_;
            const unsigned sz  = (unsigned) ringBuf_.size();
            for (int i = 0; i < winSamples_; ++i)
                window[i] = ringBuf_[(end - (unsigned) winSamples_ + (unsigned)i) % sz];

            readPos_.store (end, std::memory_order_relaxed);
            framePos_ += hopSamples_;

#if JUCE_MAC || JUCE_IOS
            if (appleHandle_)
                SoundDetectiveBridge::process (appleHandle_, window.data(),
                                               winSamples_, framePos_);
#else
            classifyHeuristic (window.data(), winSamples_);
#endif
        }
    }

    //==========================================================================
    void postEvent (const juce::String& label, float confidence)
    {
        if (confidence < threshold_) return;

        SoundEvent ev;
        ev.timestampMs = juce::Time::currentTimeMillis();
        ev.label       = label;
        ev.confidence  = confidence;

        juce::SpinLock::ScopedLockType lock (eventsLock_);
        // Debounce: skip exact same label within 1.5 s
        if (!pendingEvents_.empty())
        {
            const auto& last = pendingEvents_.back();
            if (last.label == label && (ev.timestampMs - last.timestampMs) < 1500LL)
                return;
        }
        pendingEvents_.push_back (ev);
    }

#if !(JUCE_MAC || JUCE_IOS)
    //==========================================================================
    // Simple DSP-based heuristic (Windows / Linux fallback)
    //==========================================================================
    struct HeuristicState
    {
        float prevRms        = 0.0f;
        int   sustainedLow   = 0;
        int   burstCount     = 0;
        int   burstGap       = 0;
        int   suppressFrames = 0;
    } hState_;

    void classifyHeuristic (const float* data, int n)
    {
        // 1st-order IIR band split: LP @ ~300 Hz, LP @ ~3 kHz
        const float sr  = (float) sampleRate_;
        const float aL  = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 300.0f  / sr);
        const float aH  = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 3000.0f / sr);

        float lpL = 0.0f, lpH = 0.0f;
        float sumSq = 0.0f, peak = 0.0f;
        float sumLow = 0.0f, sumMid = 0.0f, sumHigh = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float x = data[i];
            lpL += aL * (x - lpL);
            lpH += aH * (x - lpH);
            const float hi = x - lpH;
            const float mi = lpH - lpL;
            sumSq   += x * x;
            peak     = std::max (peak, std::fabs (x));
            sumLow  += lpL * lpL;
            sumMid  += mi  * mi;
            sumHigh += hi  * hi;
        }

        const float rms = std::sqrt (sumSq / n);
        if (rms < 1e-6f)
        {
            hState_.prevRms      = rms;
            hState_.sustainedLow = 0;
            hState_.burstCount   = 0;
            return;
        }

        const float total   = sumLow + sumMid + sumHigh + 1e-12f;
        const float fLow    = sumLow  / total;
        const float fMid    = sumMid  / total;
        const float fHigh   = sumHigh / total;
        const float crestDB = 20.0f * std::log10 (std::max (peak, 1e-10f) / rms);
        const float onsetDB = (hState_.prevRms > 1e-10f)
                              ? 20.0f * std::log10 (rms / hState_.prevRms) : 0.0f;
        const float rmsDB   = 20.0f * std::log10 (rms);
        hState_.prevRms = rms;

        if (rmsDB < -62.0f)
        {
            hState_.sustainedLow = 0;
            hState_.burstCount   = 0;
            return;
        }

        if (hState_.suppressFrames > 0) { --hState_.suppressFrames; return; }

        const float cf = juce::jlimit (0.0f, 1.0f, (rmsDB + 65.0f) / 35.0f);

        // Impact / door slam / clap
        if (onsetDB > 22.0f && crestDB > 10.0f && rmsDB > -45.0f)
        {
            postEvent ("Impact / Clap", 0.55f * cf);
            hState_.suppressFrames = 4;
            return;
        }

        // Whistle — narrow high-band, tonal
        if (fHigh > 0.50f && crestDB < 5.0f && rmsDB > -52.0f)
        {
            postEvent ("Whistle", 0.60f * cf);
            hState_.suppressFrames = 3;
            return;
        }

        // Alarm / Siren — mid+high sustained, tonal or sweeping
        if ((fMid + fHigh) > 0.72f && crestDB < 8.0f && rmsDB > -50.0f)
        {
            postEvent ("Alarm / Siren", 0.55f * cf);
            hState_.suppressFrames = 5;
            return;
        }

        // Dog barking — rhythmic mid-band bursts
        if (fMid > 0.50f && onsetDB > 12.0f)
        {
            ++hState_.burstCount;
            hState_.burstGap = 0;
            if (hState_.burstCount >= 3)
            {
                postEvent ("Dog Barking", 0.55f * cf);
                hState_.burstCount     = 0;
                hState_.suppressFrames = 6;
            }
        }
        else
        {
            if (++hState_.burstGap > 8) hState_.burstCount = 0;
        }

        // Passing vehicle — sustained low-frequency dominant
        hState_.sustainedLow = (fLow > 0.45f && rmsDB > -54.0f)
                               ? hState_.sustainedLow + 1 : 0;
        if (hState_.sustainedLow == 4)    // ~1 s
            postEvent ("Passing Vehicle", 0.50f * cf);
        if (hState_.sustainedLow == 14)   // ~3.5 s → aircraft
        {
            postEvent ("Aircraft", 0.55f * cf);
            hState_.sustainedLow = 0;
        }

        // Ring / doorbell — short tonal mid, strong onset
        if (fMid > 0.60f && crestDB < 4.0f && onsetDB > 12.0f && rmsDB > -50.0f)
        {
            postEvent ("Ring / Doorbell", 0.52f * cf);
            hState_.suppressFrames = 4;
        }
    }
#endif

    //==========================================================================
    bool     enabled_    = false;
    float    threshold_  = 0.35f;
    double   sampleRate_ = 44100.0;
    int      hopSamples_ = 11025;
    int      winSamples_ = 22050;
    int64_t  framePos_   = 0;

    static constexpr unsigned kRingBufSize = 262144;  // ~6 s at 44.1 kHz
    std::array<float, kRingBufSize> ringBuf_ {};
    std::atomic<unsigned>           writePos_ { 0 };
    std::atomic<unsigned>           readPos_  { 0 };

    juce::SpinLock          eventsLock_;
    std::deque<SoundEvent>  pendingEvents_;

#if JUCE_MAC || JUCE_IOS
    void* appleHandle_ = nullptr;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundDetective)
};
