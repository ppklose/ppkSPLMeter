#pragma once
#if SPLMETER_HAS_TFLITE

#include <JuceHeader.h>
#include <tensorflow/lite/c/c_api.h>
#include <vector>
#include <functional>
#include <memory>

//==============================================================================
/**
 * Wraps a .tflite model for cross-platform audio event classification.
 *
 * Expected model I/O:
 *   Input  [0]: float32  shape [1, windowSamples] or [windowSamples]
 *                        — raw mono audio normalised to ±1, at modelSampleRate
 *   Output [0]: float32  shape [1, numClasses] or [numClasses]
 *                        — per-class softmax probabilities
 *
 * Labels are loaded from an optional text file (one label per line, UTF-8).
 * Missing labels are filled with "Class N".
 *
 * Thread safety: create() and ~TFLiteDetector() must be called from a single
 * owner thread.  classify() is safe to call from any single thread, but must
 * not be called concurrently.
 */
class TFLiteDetector
{
public:
    //==========================================================================
    struct Config
    {
        juce::String modelPath;
        juce::String labelsPath;      ///< optional — auto-detected if empty
        int          modelSampleRate = 16000;
    };

    /** Returns nullptr and fills errorOut on failure. */
    static std::shared_ptr<TFLiteDetector> create (const Config& cfg,
                                                    juce::String& errorOut);

    ~TFLiteDetector()
    {
        if (interpreter_) TfLiteInterpreterDelete (interpreter_);
        if (model_)       TfLiteModelDelete       (model_);
    }

    //==========================================================================
    int              windowSamples()   const noexcept { return windowSamples_; }
    int              modelSampleRate() const noexcept { return modelSampleRate_; }
    int              numClasses()      const noexcept { return (int) labels_.size(); }
    const juce::String& modelName()   const noexcept { return modelName_; }

    /**
     * Run inference on one window already resampled to modelSampleRate.
     * Calls onEvent for every class whose score >= threshold.
     */
    void classify (const float* data, float threshold,
                   std::function<void (const juce::String&, float)> onEvent);

private:
    TFLiteDetector() = default;

    TfLiteModel*       model_       = nullptr;
    TfLiteInterpreter* interpreter_ = nullptr;
    int                windowSamples_   = 0;
    int                modelSampleRate_ = 16000;
    juce::String       modelName_;
    std::vector<juce::String> labels_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TFLiteDetector)
};

//==============================================================================
inline std::shared_ptr<TFLiteDetector>
TFLiteDetector::create (const Config& cfg, juce::String& err)
{
    auto d = std::shared_ptr<TFLiteDetector> (new TFLiteDetector());
    d->modelSampleRate_ = cfg.modelSampleRate;
    d->modelName_       = juce::File (cfg.modelPath).getFileNameWithoutExtension();

    // ---- Load model ----
    d->model_ = TfLiteModelCreateFromFile (cfg.modelPath.toRawUTF8());
    if (!d->model_)
    {
        err = "Could not load model: " + cfg.modelPath;
        return nullptr;
    }

    // ---- Create interpreter ----
    TfLiteInterpreterOptions* opts = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads (opts, 2);
    d->interpreter_ = TfLiteInterpreterCreate (d->model_, opts);
    TfLiteInterpreterOptionsDelete (opts);

    if (!d->interpreter_)
    {
        err = "Failed to create TFLite interpreter";
        return nullptr;
    }
    if (TfLiteInterpreterAllocateTensors (d->interpreter_) != kTfLiteOk)
    {
        err = "TFLite AllocateTensors failed";
        return nullptr;
    }

    // ---- Inspect input tensor — accept [N], [1,N], [1,N,1] ----
    const TfLiteTensor* in = TfLiteInterpreterGetInputTensor (d->interpreter_, 0);
    if (!in) { err = "Model has no input tensor";     return nullptr; }
    if (TfLiteTensorType (in) != kTfLiteFloat32)
        { err = "Input tensor must be float32";       return nullptr; }

    const int ndim = TfLiteTensorNumDims (in);
    d->windowSamples_ = 0;
    for (int i = ndim - 1; i >= 0; --i)
    {
        const int dim = TfLiteTensorDim (in, i);
        if (dim > 1) { d->windowSamples_ = dim; break; }
    }
    if (d->windowSamples_ < 64)
    {
        err = "Input window size implausibly small: " + juce::String (d->windowSamples_);
        return nullptr;
    }

    // ---- Inspect output tensor ----
    const TfLiteTensor* out = TfLiteInterpreterGetOutputTensor (d->interpreter_, 0);
    if (!out) { err = "Model has no output tensor"; return nullptr; }
    const int outNdim  = TfLiteTensorNumDims (out);
    const int numClasses = TfLiteTensorDim (out, outNdim - 1);

    // ---- Load labels ----
    juce::String labelsPath = cfg.labelsPath;
    if (labelsPath.isEmpty())
    {
        // Auto-detect: modelname_labels.txt or labels.txt in same directory
        const juce::File modelFile (cfg.modelPath);
        const auto candidate1 = modelFile.getSiblingFile (
            modelFile.getFileNameWithoutExtension() + "_labels.txt");
        const auto candidate2 = modelFile.getSiblingFile ("labels.txt");
        if (candidate1.existsAsFile()) labelsPath = candidate1.getFullPathName();
        else if (candidate2.existsAsFile()) labelsPath = candidate2.getFullPathName();
    }
    if (labelsPath.isNotEmpty() && juce::File (labelsPath).existsAsFile())
    {
        juce::StringArray lines;
        juce::File (labelsPath).readLines (lines);
        for (const auto& l : lines)
            if (l.trim().isNotEmpty())
                d->labels_.push_back (l.trim());
    }
    while ((int) d->labels_.size() < numClasses)
        d->labels_.push_back ("Class " + juce::String ((int) d->labels_.size()));
    d->labels_.resize ((size_t) numClasses);

    return d;
}

inline void TFLiteDetector::classify (const float* data, float threshold,
                                       std::function<void (const juce::String&, float)> onEvent)
{
    if (!interpreter_) return;

    TfLiteTensor* in = TfLiteInterpreterGetInputTensor (interpreter_, 0);
    if (!in) return;
    if (TfLiteTensorCopyFromBuffer (in, data, (size_t) windowSamples_ * sizeof (float)) != kTfLiteOk)
        return;
    if (TfLiteInterpreterInvoke (interpreter_) != kTfLiteOk) return;

    const TfLiteTensor* out = TfLiteInterpreterGetOutputTensor (interpreter_, 0);
    if (!out) return;

    const int n = (int) labels_.size();
    std::vector<float> scores ((size_t) n);
    if (TfLiteTensorCopyToBuffer (out, scores.data(), (size_t) n * sizeof (float)) != kTfLiteOk)
        return;

    for (int i = 0; i < n; ++i)
        if (scores[i] >= threshold)
            onEvent (labels_[i], scores[i]);
}

#endif // SPLMETER_HAS_TFLITE
