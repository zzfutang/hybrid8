//
//  SynthDSPKernelAdapter.mm
//  Bridges Swift <-> C++ and implements the real-time render loop, splitting
//  each render call around scheduled MIDI and parameter-automation events.
//

#import "SynthDSPKernelAdapter.h"
#import "DSP/SynthEngine.hpp"

@implementation SynthDSPKernelAdapter {
    synth::SynthEngine _engine;
    double _sampleRate;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _sampleRate = 44100.0;
        _engine.setSampleRate(_sampleRate);
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

- (AUInternalRenderBlock)internalRenderBlock {
    // Capture a raw pointer to the C++ engine so the audio thread never does
    // ARC retain/release or ObjC message sends.
    synth::SynthEngine *engine = &_engine;

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
                    case AURenderEventParameter:
                    case AURenderEventParameterRamp: {
                        const AUParameterEvent &pe = event->parameter;
                        engine->setParameter((uint64_t)pe.parameterAddress, (float)pe.value);
                        break;
                    }
                    case AURenderEventMIDI: {
                        const AUMIDIEvent &m = event->MIDI;
                        const uint8_t status  = m.data[0] & 0xF0;
                        const uint8_t d1 = (m.length > 1) ? m.data[1] : 0;
                        const uint8_t d2 = (m.length > 2) ? m.data[2] : 0;
                        switch (status) {
                            case 0x90: engine->noteOn(d1, d2); break;           // note on
                            case 0x80: engine->noteOff(d1); break;              // note off
                            case 0xE0: engine->pitchBend((int)d1 | ((int)d2 << 7)); break;
                            case 0xB0: // control change
                                if (d1 == 1) engine->modWheel(d2 / 127.0f);   // mod wheel
                                else if (d1 == 120) engine->allSoundOff();
                                else if (d1 == 123) engine->allNotesOff();
                                break;
                            case 0xD0: engine->aftertouch(d1 / 127.0f); break; // channel pressure
                            case 0xA0: engine->aftertouch(d2 / 127.0f); break; // poly key pressure
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
