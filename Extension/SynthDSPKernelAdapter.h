//
//  SynthDSPKernelAdapter.h
//  Objective-C bridge exposed to Swift. Wraps the C++ SynthEngine and provides
//  the real-time render block plus thread-safe parameter access. This is the
//  only C++-touching interface Swift needs to see (via the bridging header).
//

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SynthDSPKernelAdapter : NSObject

/// Configure DSP for the given stream format. Call before allocating resources.
- (void)setSampleRate:(double)sampleRate channelCount:(AVAudioChannelCount)channelCount;

/// Reset all voices / filters (called on transport stop or bypass).
- (void)reset;

/// Thread-safe parameter set/get (address == SynthParam value).
- (void)setParameter:(AUParameterAddress)address value:(AUValue)value;
- (AUValue)valueForParameter:(AUParameterAddress)address;
- (AUValue)effectiveValueForParameter:(AUParameterAddress)address;
- (AUValue)compressorGainReductionDB;
- (AUValue)outputMeterLeft;
- (AUValue)outputMeterRight;

/// Build and publish an imported wavetable. This performs offline allocation
/// and spectral processing and must never be called from the render thread.
- (BOOL)installWavetableAtSlot:(NSInteger)slot
                       samples:(NSData *)samples
                   frameLength:(NSInteger)frameLength;

/// The real-time render block handed to the AUAudioUnit.
- (AUInternalRenderBlock)internalRenderBlockWithMusicalContext:
    (nullable AUHostMusicalContextBlock)musicalContext;

@end

NS_ASSUME_NONNULL_END
