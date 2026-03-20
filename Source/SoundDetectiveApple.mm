// SoundDetectiveApple.mm
// Apple SoundAnalysis ML backend for SoundDetective.
// Compiled only on macOS / iOS (see CMakeLists.txt).

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <SoundAnalysis/SoundAnalysis.h>

#include "SoundDetective.h"

//==============================================================================
// Map Apple's internal sound identifiers to user-friendly names.
static juce::String friendlyLabel (NSString* appleId)
{
    // We match by substring so partial matches work across OS versions.
    static const struct { const char* key; const char* friendly; } kMap[] = {
        { "dog",               "Dog Barking"       },
        { "cat",               "Cat"               },
        { "bird",              "Bird"              },
        { "car_horn",          "Car Horn"          },
        { "car",               "Car / Vehicle"     },
        { "vehicle",           "Vehicle"           },
        { "motorcycle",        "Motorcycle"        },
        { "truck",             "Truck"             },
        { "bus",               "Bus"               },
        { "aircraft",          "Aircraft"          },
        { "airplane",          "Airplane"          },
        { "helicopter",        "Helicopter"        },
        { "jet",               "Jet"               },
        { "doorbell",          "Doorbell"          },
        { "door",              "Door"              },
        { "glass",             "Glass Breaking"    },
        { "gun",               "Gunshot"           },
        { "alarm",             "Alarm"             },
        { "siren",             "Siren"             },
        { "smoke",             "Smoke Alarm"       },
        { "fire",              "Fire Alarm"        },
        { "thunder",           "Thunder"           },
        { "rain",              "Rain"              },
        { "wind",              "Wind"              },
        { "whistle",           "Whistle"           },
        { "music",             "Music"             },
        { "speech",            "Speech"            },
        { "baby",              "Baby Crying"       },
        { "applause",          "Applause"          },
        { "crowd",             "Crowd"             },
        { "laughter",          "Laughter"          },
        { "telephone",         "Phone Ring"        },
        { "phone",             "Phone Ring"        },
        { "chainsaw",          "Chainsaw"          },
        { "drill",             "Power Tool"        },
        { "hammer",            "Hammering"         },
        { "construction",      "Construction"      },
        { "explosion",         "Explosion"         },
        { "firework",          "Fireworks"         },
        { "water",             "Water"             },
        { nullptr,             nullptr             }
    };

    juce::String id (appleId.UTF8String);
    for (int i = 0; kMap[i].key != nullptr; ++i)
        if (id.containsIgnoreCase (kMap[i].key))
            return kMap[i].friendly;

    // Fallback: capitalise and replace underscores
    return id.replace ("_", " ").trim();
}

//==============================================================================
// Pimpl struct holding the ObjC objects
struct AppleDetectorImpl
{
    SNAudioStreamAnalyzer*                       analyzer  = nil;
    id<SNResultsObserving>                        observer  = nil;
    AVAudioFormat*                               format    = nil;
    AVAudioFramePosition                         framePos  = 0;
    float                                        threshold = 0.35f;
    dispatch_queue_t                             queue     = nullptr;
    std::function<void(const juce::String&, float)> onEvent;
};

//==============================================================================
@interface SoundDetObserver : NSObject <SNResultsObserving>
- (instancetype)initWithImpl:(AppleDetectorImpl*)impl;
@end

@implementation SoundDetObserver {
    AppleDetectorImpl* _impl;
}

- (instancetype)initWithImpl:(AppleDetectorImpl*)impl {
    if ((self = [super init])) _impl = impl;
    return self;
}

- (void)request:(id<SNRequest>)request didProduceResult:(id<SNResult>)result {
    SNClassificationResult* cr = (SNClassificationResult*)result;
    for (SNClassification* cl in cr.classifications) {
        if ((float)cl.confidence >= _impl->threshold) {
            _impl->onEvent (friendlyLabel (cl.identifier), (float)cl.confidence);
            break;   // only report highest-confidence class per window
        }
    }
}

- (void)request:(id<SNRequest>)request didFailWithError:(NSError*)error {}
- (void)requestDidComplete:(id<SNRequest>)request {}

@end

//==============================================================================
namespace SoundDetectiveBridge
{
    void* create (double sampleRate, float threshold,
                  std::function<void(const juce::String&, float)> onEvent)
    {
        if (@available (macOS 11.0, iOS 14.0, *))
        {
            auto* impl     = new AppleDetectorImpl();
            impl->threshold = threshold;
            impl->onEvent   = std::move (onEvent);

            impl->queue = dispatch_queue_create ("com.splmeter.sounddetective",
                                                   DISPATCH_QUEUE_SERIAL);

            impl->format = [[AVAudioFormat alloc]
                            initWithCommonFormat: AVAudioPCMFormatFloat32
                                     sampleRate: sampleRate
                                       channels: 1
                                    interleaved: NO];

            impl->analyzer = [[SNAudioStreamAnalyzer alloc]
                              initWithFormat: impl->format];

            impl->observer = [[SoundDetObserver alloc] initWithImpl: impl];

            NSError* error = nil;
            SNClassifySoundRequest* req = [[SNClassifySoundRequest alloc]
                initWithClassifierIdentifier: SNClassifierIdentifierVersion1
                                       error: &error];
            if (!req || error) { delete impl; return nullptr; }

            if (@available (macOS 12.0, iOS 15.0, *))
            {
                req.windowDuration = CMTimeMakeWithSeconds (1.0, NSEC_PER_SEC);
                req.overlapFactor  = 0.5;
            }

            if (![impl->analyzer addRequest: req
                               withObserver: impl->observer
                                      error: &error]
                || error)
            {
                delete impl;
                return nullptr;
            }

            return impl;
        }
        return nullptr;
    }

    void destroy (void* handle) noexcept
    {
        if (auto* impl = static_cast<AppleDetectorImpl*> (handle))
        {
            // Wait for all in-flight async blocks to finish before freeing impl.
            if (impl->queue)
                dispatch_sync (impl->queue, ^{});
            delete impl;
        }
    }

    void process (void* handle, const float* data, int numSamples,
                  int64_t framePos) noexcept
    {
        auto* impl = static_cast<AppleDetectorImpl*> (handle);
        if (!impl || !impl->analyzer || !impl->format || !impl->queue) return;

        // Copy audio data into a heap block captured by the async dispatch.
        // The caller's buffer is only valid until process() returns.
        auto* samples = new std::vector<float> (data, data + numSamples);
        const AVAudioFramePosition pos = (AVAudioFramePosition) framePos;

        dispatch_async (impl->queue, ^{
            @autoreleasepool {
                if (@available (macOS 10.15, iOS 13.0, *))
                {
                    AVAudioPCMBuffer* buf = [[AVAudioPCMBuffer alloc]
                                             initWithPCMFormat: impl->format
                                                 frameCapacity: (AVAudioFrameCount) samples->size()];
                    buf.frameLength = (AVAudioFrameCount) samples->size();
                    std::memcpy (buf.floatChannelData[0], samples->data(),
                                 samples->size() * sizeof (float));
                    [impl->analyzer analyzeAudioBuffer: buf atAudioFramePosition: pos];
                }
                delete samples;
            }
        });
    }

    void setThreshold (void* handle, float threshold) noexcept
    {
        if (auto* impl = static_cast<AppleDetectorImpl*> (handle))
            impl->threshold = threshold;
    }
}
