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

/// Lock-free parameter set/get from any thread (address == R50Param value).
/// Writes land in an atomic store; the engine derives its live DSP state from
/// that store on the render thread only.
- (void)setParameter:(AUParameterAddress)address value:(AUValue)value;
- (AUValue)valueForParameter:(AUParameterAddress)address;

/// Peak output level of the last render quantum, for the UI meter.
- (AUValue)outputMeter;

/// Decode-side sample installation. Allocates and copies, so it must never be
/// called from the render thread. Adds the asset plus a single-region
/// instrument covering the whole keyboard, and returns the new instrument
/// index, or -1 if the library is full.
- (NSInteger)installSampleNamed:(NSString *)name
                        samples:(NSData *)samples
                     sampleRate:(double)sampleRate
                        rootKey:(NSInteger)rootKey
                       loopMode:(NSInteger)loopMode;

/// Catalog for the editor's sample browser.
- (NSInteger)instrumentCount;
- (nullable NSString *)instrumentNameAtIndex:(NSInteger)index;

/// Everything the browser table shows about one entry: name, zone count, key
/// span, loop mode, the duration of the zone covering middle C, and the total
/// audio it occupies. Read off the published library, so it is only ever called
/// from the UI thread.
- (nullable NSDictionary<NSString *, id> *)sampleInfoAtIndex:(NSInteger)index;

/// The real-time render block handed to the AUAudioUnit.
- (AUInternalRenderBlock)internalRenderBlock;

@end

NS_ASSUME_NONNULL_END
