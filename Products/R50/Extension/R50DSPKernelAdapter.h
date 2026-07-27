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
/// `loopEnd` of 0 means the whole buffer, which is what a caller with no loop
/// points to offer should pass.
- (NSInteger)installSampleNamed:(NSString *)name
                        samples:(NSData *)samples
                     sampleRate:(double)sampleRate
                        rootKey:(NSInteger)rootKey
                       loopMode:(NSInteger)loopMode
                      loopStart:(NSInteger)loopStart
                        loopEnd:(NSInteger)loopEnd;

/// Catalog for the editor's sample browser.
- (NSInteger)instrumentCount;
- (nullable NSString *)instrumentNameAtIndex:(NSInteger)index;

/// Everything the browser table shows about one entry: name, zone count, key
/// span, loop mode, the duration of the zone covering middle C, and the total
/// audio it occupies. Read off the published library, so it is only ever called
/// from the UI thread.
- (nullable NSDictionary<NSString *, id> *)sampleInfoAtIndex:(NSInteger)index;

/// Where the factory WAV files live. Inside the sandbox container, so it is
/// not somewhere anyone would find by guessing — the editor shows it.
- (NSString *)factoryDirectory;

/// One zone encoded as a ready-to-write WAV, so generated content can be
/// opened in an editor. The bytes carry a `smpl` chunk with the loop points,
/// which is the most valuable part of a generated sustain. UI thread only.
- (nullable NSDictionary<NSString *, id> *)zoneOfInstrument:(NSInteger)index
                                                       zone:(NSInteger)zone;

/// What a file says about itself: root key and loop, read from its `smpl`
/// chunk by the same parser the factory loader uses. Returns nil for anything
/// that is not a WAV, or a WAV with no `smpl` chunk — which is the caller's cue
/// to fall back to detecting the pitch.
///
/// Only the metadata is read here. The audio still goes through AVAudioFile,
/// which handles the formats this parser does not.
- (nullable NSDictionary<NSString *, id> *)metadataOfFileAtPath:(NSString *)path;

/// Estimate the pitch of a decoded mono buffer. Returns nil when the material
/// has no period worth trusting — a noise burst has no root key, and inventing
/// one for it is worse than declining.
- (nullable NSDictionary<NSString *, id> *)detectPitchOf:(NSData *)samples
                                              sampleRate:(double)sampleRate;

/// Retune an imported instrument. Lock-free and safe while audio runs: the
/// next note-on picks it up, and notes already sounding keep their pitch.
- (void)setRootKey:(NSInteger)rootKey
         tuneCents:(float)tuneCents
    forInstrument:(NSInteger)index;

/// Preview one browser entry. Lock-free: this only posts a request that the
/// render thread picks up on its next control block, so it is safe to call from
/// the UI thread while audio is running. The preview bypasses the patch and the
/// voice pool — see R50Engine::requestAudition.
- (void)auditionInstrumentAtIndex:(NSInteger)index
                             note:(uint8_t)note
                         velocity:(uint8_t)velocity;

/// The real-time render block handed to the AUAudioUnit.
- (AUInternalRenderBlock)internalRenderBlock;

@end

NS_ASSUME_NONNULL_END
