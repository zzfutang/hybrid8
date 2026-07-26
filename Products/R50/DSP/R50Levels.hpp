//
//  R50Levels.hpp
//  The instrument's level convention, in one place because every source has to
//  agree on it.
//
//  Sources are matched by RMS, not by peak. Peak-matching sounds like it should
//  work and does not: it left a sparse spectrum such as the bell nearly 11 dB
//  quieter than a dense one such as the clarinet, and samples 2 to 6 dB below
//  the wave tables, because equal peaks say nothing about how loud something
//  is. Switching a Partial from one source to another should change its timbre
//  and not its level.
//
//  The peak ceiling is what keeps that safe. A high-crest-factor source — a
//  narrow pulse especially — needs a lot of gain to reach the RMS target, so
//  the ceiling caps it and it settles slightly below target rather than
//  swamping the voice sum.
//

#pragma once

namespace r50 {

static constexpr float kSourceTargetRms   = 0.50f;
static constexpr float kSourcePeakCeiling = 1.30f;

} // namespace r50
