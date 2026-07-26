//
//  R50DSPKernelAdapter.h
//  Objective-C bridge exposed to Swift. Wraps the C++ R50Engine and provides
//  the real-time render block plus thread-safe parameter access. This is the
//  only C++-touching interface Swift needs to see (via the bridging header).
//

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface R50DSPKernelAdapter : NSObject

/// Configure DSP for the given stream format. Call before allocating resources.
- (void)setSampleRate:(double)sampleRate channelCount:(AVAudioChannelCount)channelCount;

/// Reset all voices / filters (called on transport stop or bypass).
- (void)reset;

/// Thread-safe parameter set/get (address == R50Param value).
- (void)setParameter:(AUParameterAddress)address value:(AUValue)value;
- (AUValue)valueForParameter:(AUParameterAddress)address;

/// Peak output level of the last render quantum, for the UI meter.
- (AUValue)outputMeter;

/// The real-time render block handed to the AUAudioUnit.
- (AUInternalRenderBlock)internalRenderBlock;

@end

NS_ASSUME_NONNULL_END
