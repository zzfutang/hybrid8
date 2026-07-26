//
//  R50DSPKernelAdapter.mm
//  Bridges Swift <-> C++ and implements the real-time render loop, splitting
//  each render call around scheduled MIDI and parameter-automation events.
//

#import "R50DSPKernelAdapter.h"
#import "R50PitchDetect.hpp"
#import "R50WavWriter.hpp"
#import "R50FactoryFiles.hpp"

#include <mutex>
#import "R50Engine.hpp"

/// Application Support/R50/Factory. Resolved here rather than in C++ because
/// the sandbox container is a Foundation question, and done once per process.
static std::string R50FactoryDirectory(void) {
    NSArray<NSURL *> *urls = [NSFileManager.defaultManager
        URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
    if (urls.count == 0) return std::string();
    NSURL *directory = [[urls.firstObject URLByAppendingPathComponent:@"R50"]
                        URLByAppendingPathComponent:@"Factory"];
    [NSFileManager.defaultManager createDirectoryAtURL:directory
                           withIntermediateDirectories:YES
                                            attributes:nil error:nil];
    return std::string(directory.path.UTF8String ?: "");
}

@implementation R50DSPKernelAdapter {
    r50::R50Engine _engine;
    double _sampleRate;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _sampleRate = 44100.0;
        _engine.setSampleRate(_sampleRate);

        // Mirror the generated content to disk, and load back anything that is
        // already there. Once per process — the library is a process-wide
        // singleton, so a second instance would only redo the same work.
        static std::once_flag once;
        std::call_once(once, []{
            r50::SampleLibrary &library = r50::SampleLibrary::shared();

            const std::string factory = R50FactoryDirectory();
            if (!factory.empty()) r50::syncFactoryDirectory(library, factory);

            // Repository samples, built into the bundle. Deliberately not the
            // container's Samples folder as well: that holds imports, keyed by
            // library.json under UUID filenames, and scanning it too would load
            // every imported sample a second time under its UUID.
            NSURL *bundled = [[NSBundle bundleForClass:R50DSPKernelAdapter.class]
                              URLForResource:@"Samples" withExtension:nil];
            if (bundled != nil) {
                r50::loadSampleDirectory(library, bundled.path.UTF8String);
            }

        });
    }
    return self;
}

- (void)setSampleRate:(double)sampleRate channelCount:(AVAudioChannelCount)channelCount {
    _sampleRate = sampleRate;
    _engine.setSampleRate(sampleRate);
}

- (void)reset {
    _engine.allSoundOff();
}

- (void)setParameter:(AUParameterAddress)address value:(AUValue)value {
    _engine.setParameter((uint64_t)address, (float)value);
}

- (AUValue)valueForParameter:(AUParameterAddress)address {
    return (AUValue)_engine.getParameter((uint64_t)address);
}

- (AUValue)outputMeter {
    return (AUValue)_engine.outputMeter();
}

- (NSInteger)installSampleNamed:(NSString *)name
                        samples:(NSData *)samples
                     sampleRate:(double)sampleRate
                        rootKey:(NSInteger)rootKey
                       loopMode:(NSInteger)loopMode {
    if (samples.length == 0 || samples.length % sizeof(float) != 0) return -1;

    const float *values = (const float *)samples.bytes;
    const int count = (int)(samples.length / sizeof(float));

    r50::SampleData data;
    data.samples.assign(values, values + count);
    data.sourceSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    data.rootKey   = (int)rootKey;
    data.loopStart = 0;
    data.loopEnd   = (uint32_t)count;
    data.loopMode  = (r50::LoopMode)loopMode;

    r50::SampleLibrary &library = r50::SampleLibrary::shared();
    const int slot = library.addSample(std::move(data));
    if (slot < 0) return -1;

    r50::Multisample instrument;
    instrument.setName(name.UTF8String ?: "Imported");
    r50::SampleRegion region;
    region.rootKey = (int)rootKey;
    region.slot    = slot;
    instrument.regions[0] = region;
    instrument.regionCount = 1;
    return library.addInstrument(instrument);
}

- (NSString *)factoryDirectory {
    const std::string directory = R50FactoryDirectory();
    return [NSString stringWithUTF8String:directory.c_str()];
}

- (NSDictionary<NSString *, id> *)zoneOfInstrument:(NSInteger)index
                                              zone:(NSInteger)zone {
    r50::SampleLibrary &library = r50::SampleLibrary::shared();
    const r50::Multisample *instrument = library.instrument((int)index);
    if (instrument == nullptr || zone < 0 || zone >= instrument->regionCount) {
        return nil;
    }
    const r50::SampleRegion &region = instrument->regions[zone];
    const r50::SampleData *data = library.sample(region.slot);
    if (data == nullptr || data->samples.empty()) return nil;

    const std::vector<uint8_t> wav =
        r50::encodeWav(data->samples.data(), data->length(),
                       data->sourceSampleRate, region.rootKey,
                       data->loopStart, data->loopEnd,
                       data->loopMode != r50::LoopMode::None);

    return @{
        @"name":       [NSString stringWithUTF8String:instrument->name],
        @"wav":        [NSData dataWithBytes:wav.data() length:wav.size()],
        @"sampleRate": @(data->sourceSampleRate),
        @"rootKey":    @(region.rootKey),
        @"lowKey":     @(region.lowKey),
        @"highKey":    @(region.highKey),
        @"loopStart":  @(data->loopStart),
        @"loopEnd":    @(data->loopEnd),
        @"loopMode":   @((int)data->loopMode),
    };
}

- (NSDictionary<NSString *, id> *)detectPitchOf:(NSData *)samples
                                     sampleRate:(double)sampleRate {
    if (samples.length < sizeof(float)) return nil;
    const r50::DetectedPitch found =
        r50::detectPitch((const float *)samples.bytes,
                         (int)(samples.length / sizeof(float)), sampleRate);
    if (!found.valid) return nil;
    return @{
        @"hertz":      @(found.hertz),
        @"rootKey":    @(found.rootKey),
        @"centsSharp": @(found.centsSharp),
        @"confidence": @(found.confidence),
    };
}

- (void)setRootKey:(NSInteger)rootKey
         tuneCents:(float)tuneCents
    forInstrument:(NSInteger)index {
    r50::RootTuning tuning;
    tuning.rootKey   = (int)rootKey;
    tuning.tuneCents = tuneCents;
    r50::SampleLibrary::shared().setRootTuning((int)index, tuning);
}

- (void)auditionInstrumentAtIndex:(NSInteger)index
                             note:(uint8_t)note
                         velocity:(uint8_t)velocity {
    _engine.requestAudition(static_cast<int>(index), note, velocity);
}

- (NSInteger)instrumentCount {
    return r50::SampleLibrary::shared().instrumentCount();
}

- (NSString *)instrumentNameAtIndex:(NSInteger)index {
    const r50::Multisample *instrument =
        r50::SampleLibrary::shared().instrument((int)index);
    if (instrument == nullptr) return nil;
    return [NSString stringWithUTF8String:instrument->name];
}

- (NSDictionary<NSString *, id> *)sampleInfoAtIndex:(NSInteger)index {
    r50::SampleLibrary &library = r50::SampleLibrary::shared();
    const r50::Multisample *instrument = library.instrument((int)index);
    if (instrument == nullptr || instrument->regionCount <= 0) return nil;

    int lowKey = 127, highKey = 0, loopMode = 0;
    size_t totalFrames = 0, representativeFrames = 0;
    double sampleRate = 44100.0;

    for (int r = 0; r < instrument->regionCount; ++r) {
        const r50::SampleRegion &region = instrument->regions[r];
        lowKey  = std::min(lowKey, region.lowKey);
        highKey = std::max(highKey, region.highKey);

        const r50::SampleData *data = library.sample(region.slot);
        if (data == nullptr) continue;
        totalFrames += data->samples.size();
        sampleRate = data->sourceSampleRate;

        // Report the zone a player is most likely to hear first.
        const bool coversMiddleC = region.lowKey <= 60 && region.highKey >= 60;
        if (r == 0 || coversMiddleC) {
            loopMode = (int)data->loopMode;
            representativeFrames = data->samples.size();
        }
    }

    // The effective root: the library override when one has been set for a
    // single-region import, otherwise the region's own.
    int rootKey = instrument->regions[0].rootKey;
    float tuneCents = instrument->regions[0].tuneCents;
    r50::RootTuning tuning;
    const bool retunable = instrument->regionCount == 1;
    if (retunable && library.rootTuning((int)index, tuning)) {
        rootKey   = tuning.rootKey;
        tuneCents = tuning.tuneCents;
    }

    return @{
        @"name":       [NSString stringWithUTF8String:instrument->name],
        @"zones":      @(instrument->regionCount),
        @"rootKey":    @(rootKey),
        @"tuneCents":  @(tuneCents),
        @"retunable":  @(retunable),
        @"lowKey":     @(lowKey),
        @"highKey":    @(highKey),
        @"loopMode":   @(loopMode),
        @"frames":     @(representativeFrames),
        @"totalBytes": @(totalFrames * sizeof(float)),
        @"sampleRate": @(sampleRate),
    };
}

- (AUInternalRenderBlock)internalRenderBlock {
    // Capture a raw pointer to the C++ engine so the audio thread never does
    // ARC retain/release or ObjC message sends.
    r50::R50Engine *engine = &_engine;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags *actionFlags,
                              const AudioTimeStamp       *timestamp,
                              AVAudioFrameCount           frameCount,
                              NSInteger                   outputBusNumber,
                              AudioBufferList            *outputData,
                              const AURenderEvent        *realtimeEventListHead,
                              AURenderPullInputBlock      pullInputBlock) {
        // --- Resolve output buffers (non-interleaved float) -----------------
        float *outL = (float *)outputData->mBuffers[0].mData;
        float *outR = (outputData->mNumberBuffers > 1)
                        ? (float *)outputData->mBuffers[1].mData
                        : outL;
        if (outL == NULL) {
            return kAudioUnitErr_InvalidParameter;
        }

        // --- Walk the event list, rendering the audio between events --------
        AUEventSampleTime now = (AUEventSampleTime)timestamp->mSampleTime;
        AUAudioFrameCount framesRemaining = frameCount;
        AUAudioFrameCount bufferOffset = 0;
        const AURenderEvent *event = realtimeEventListHead;

        while (framesRemaining > 0) {
            if (event == NULL) {
                engine->render(outL + bufferOffset, outR + bufferOffset, (int)framesRemaining);
                break;
            }

            AUEventSampleTime eventTime = event->head.eventSampleTime;

            // Any event due now or in the past: apply it immediately.
            if (eventTime <= now) {
                switch (event->head.eventType) {
                    case AURenderEventParameter: {
                        const AUParameterEvent &pe = event->parameter;
                        engine->setParameter((uint64_t)pe.parameterAddress, (float)pe.value);
                        break;
                    }
                    case AURenderEventParameterRamp: {
                        const AUParameterEvent &pe = event->parameter;
                        engine->startParameterRamp(
                            (uint64_t)pe.parameterAddress, (float)pe.value,
                            (uint32_t)pe.rampDurationSampleFrames);
                        break;
                    }
                    case AURenderEventMIDI: {
                        const AUMIDIEvent &m = event->MIDI;
                        const uint8_t status = m.data[0] & 0xF0;
                        const uint8_t d1 = (m.length > 1) ? m.data[1] : 0;
                        const uint8_t d2 = (m.length > 2) ? m.data[2] : 0;
                        switch (status) {
                            case 0x90: engine->noteOn(d1, d2); break;
                            case 0x80: engine->noteOff(d1); break;
                            case 0xE0: engine->pitchBend((int)d1 | ((int)d2 << 7)); break;
                            case 0xB0: // control change
                                if (d1 == 1) engine->modWheel(d2 / 127.0f);
                                else if (d1 == 64) engine->sustainPedal(d2 >= 64);
                                else if (d1 == 120) engine->allSoundOff();
                                else if (d1 == 123) engine->allNotesOff();
                                break;
                            case 0xD0: engine->aftertouch(d1 / 127.0f); break;
                            case 0xA0: engine->aftertouch(d2 / 127.0f); break;
                            default: break;
                        }
                        break;
                    }
                    default:
                        break; // ignore SysEx / MIDI 2.0 UMP
                }
                event = event->head.next;
                continue;
            }

            // Otherwise render up to the next event.
            int64_t diff = (int64_t)(eventTime - now);
            AUAudioFrameCount seg = (diff > (int64_t)framesRemaining)
                                        ? framesRemaining
                                        : (AUAudioFrameCount)diff;
            engine->render(outL + bufferOffset, outR + bufferOffset, (int)seg);
            bufferOffset    += seg;
            framesRemaining -= seg;
            now             += seg;
        }

        return noErr;
    };
}

@end
