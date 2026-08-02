#!/usr/bin/env python3
"""Convert 20 selected Korg M1 preload programs into R50 patch documents.

Mapping notes:
- M1 osc1/osc2 -> R50 Tone A partials 1/2 (Layer, Mix structure).
- M1 VDA/VDF 6-segment EGs map 1:1 onto R50's extended EG
  (Attack/AttackLevel -> Decay -> Break -> Slope -> Sustain -> Release).
- Multisounds map to the closest R50 sample instrument or wavetable.
- The two M1 effects map onto FX slots 1/2 as inserts in series.
- Gaps that R50 cannot express are collected per program and printed.
"""
import json, math, os

M1 = json.load(open('/Users/johangorsjo/code/r50_tools/korg_m1_preload.json'))
OUT = '/Users/johangorsjo/code/synth/Products/R50/factory_presets'
PROGRAMS = {p['program_number']: p for p in M1['programs']}

# waveform indices: 0 Saw 1 Tri 2 Square 3 Pulse10 4 Pulse 5 Organ 6 Tine
# 7 Clarinet 8 Strings 9 VocalAh 10 Bell 11 Sine ...
WAVE = {'Saw': 0, 'Tri': 1, 'Square': 2, 'Pulse10': 3, 'Pulse': 4,
        'Organ': 5, 'Tine': 6, 'Strings': 8, 'Bell': 10, 'Sine': 11}

# fx algorithm indices per R50 effectAlgorithmNames
ALG = {'Off': 0, 'Hall': 1, 'Room': 2, 'Plate': 3, 'Early': 4,
       'StereoDelay': 5, 'CrossDelay': 6, 'Chorus': 7, 'Ensemble': 8,
       'Flanger': 9, 'Phaser': 10, 'Tremolo': 11, 'Rotary': 12,
       'EQ': 13, 'Overdrive': 14, 'Distortion': 15, 'Exciter': 16}

MOD_SOURCE = {'lfo1': 1, 'velocity': 6, 'wheel': 8}
MOD_DEST = {'pitch': 1, 'cutoff': 2}

# program -> (r50 name, per-osc source spec, extras)
# source spec: ('sample', persistentID) or ('wave', index) or
# ('wave', index, noiseMix)
SELECTION = {
    'I00': ('M1 • Universe',
            {'osc1': ('sample', 'factory.ahh_choir'),
             'osc2': ('sample', 'factory.glass_pad')}, {}),
    'I01': ('M1 • Piano 16\'',
            {'osc1': ('sample', 'factory.piano_multisample')}, {}),
    'I03': ('M1 • Ooh/Ahh',
            {'osc1': ('sample', 'factory.ooh_choir'),
             'osc2': ('sample', 'factory.ahh_choir')}, {}),
    'I07': ('M1 • Symphonic',
            {'osc1': ('sample', 'factory.strings'),
             'osc2': ('wave', WAVE['Saw'])}, {}),
    'I08': ('M1 • Pan Flute',
            {'osc1': ('sample', 'factory.pan_flute')}, {}),
    'I11': ('M1 • E.Piano 1',
            {'osc1': ('wave', WAVE['Tine'])},
            {'gap': 'no electric-piano multisound; wavetable tine stands in'}),
    'I15': ('M1 • Vibes',
            {'osc1': ('sample', 'factory.vibraphone')}, {}),
    'I17': ('M1 • Organ 2',
            {'osc1': ('sample', 'factory.percussive_organ')}, {}),
    'I19': ('M1 • Pole',
            {'osc1': ('sample', 'factory.ooh_choir', 0.14)},
            {'gap': 'Pole multisound approximated by ooh choir plus breath noise'}),
    'I20': ('M1 • Dream Pad',
            {'osc1': ('wave', WAVE['Saw']),
             'osc2': ('sample', 'factory.spectrum_2')}, {}),
    'I23': ('M1 • Choir',
            {'osc1': ('sample', 'factory.ahh_choir')}, {}),
    'I27': ('M1 • Strings',
            {'osc1': ('sample', 'factory.strings')}, {}),
    'I30': ('M1 • Lore',
            {'osc1': ('sample', 'factory.glass_pad', 0.16)},
            {'gap': 'Lore multisound approximated by glass pad plus breath noise'}),
    'I42': ('M1 • Overture',
            {'osc1': ('sample', 'factory.brass_section'),
             'osc2': ('sample', 'factory.strings')}, {}),
    'I47': ('M1 • Pipe Organ',
            {'osc1': ('sample', 'factory.church_organ')}, {}),
    'I60': ('M1 • Cloud Nine',
            {'osc1': ('wave', WAVE['Bell']),
             'osc2': ('sample', 'factory.strings')}, {}),
    'I67': ('M1 • Organ 1',
            {'osc1': ('sample', 'factory.percussive_organ')}, {}),
    'I75': ('M1 • Digi-Bells',
            {'osc1': ('wave', WAVE['Bell'])}, {}),
    'I90': ('M1 • Zephyr',
            {'osc1': ('sample', 'factory.ooh_choir', 0.10),
             'osc2': ('sample', 'factory.strings')},
            {'gap': 'Pole multisound approximated by ooh choir plus breath noise'}),
    'I92': ('M1 • SynthBrass',
            {'osc1': ('wave', WAVE['Saw']),
             'osc2': ('wave', WAVE['Saw'])}, {}),
}

def t99(v, ceiling=8.0):
    """M1 0-99 EG time -> seconds (perceptual power curve)."""
    return round(max(0.001, min(ceiling, 0.002 + (v / 99.0) ** 2.7 * 9.0)), 4)

def l99(v):
    return round(max(0.0, min(1.0, v / 99.0)), 4)

def level99(v):
    """M1 oscillator level -> linear amplitude. The M1's level knob is
    perceptual: 30 of 99 is a full-sounding organ, nowhere near 30%."""
    return round(max(0.0, min(1.0, (v / 99.0) ** 0.55)), 4)

def cutoff_hz(v):
    """M1 VDF position 0-99 -> Hz."""
    return round(80.0 * 2.0 ** (v / 99.0 * 7.5), 1)

def mg_hz(v):
    return round(max(0.01, min(20.0, 0.1 * 2.0 ** (v / 9.0))), 3)

def reverb_decay_control(seconds):
    return round(math.log(max(0.4, min(30.0, seconds)) / 0.4) / math.log(30.0), 4)

def delay_time_control(seconds):
    return round(math.log(max(0.001, min(2.0, seconds)) / 0.001) / math.log(2000.0), 4)

def delay_feedback_control(amount):
    return round((max(-0.95, min(0.95, amount)) + 0.95) / 1.9, 4)

def chorus_rate_control(hz):
    return round(math.log(max(0.05, hz) / 0.05) / math.log(160.0), 4)

OCTAVE = {"32'": -2, "16'": -1, "8'": 0, "4'": 1}

def convert_effect(fx, values, slot, extra_slot_state):
    kind = fx['type']
    raw = fx['parameters_raw']
    mix = max(0.05, min(0.6, ((fx['l_channel_balance']
                               + fx['r_channel_balance']) / 2) / 100.0))
    p = f'fx.fxSlot{slot}'
    def alg(name): values[p + 'Algorithm'] = ALG[name]
    if kind in ('Hall', 'Ensemble Hall', 'Concert Hall'):
        alg('Hall')
        values[p + 'Control2'] = reverb_decay_control(raw[0] / 10.0)
    elif kind in ('Room', 'Large Room'):
        alg('Room')
        values[p + 'Control2'] = reverb_decay_control(max(0.4, raw[0] / 10.0))
    elif kind == 'Live Stage':
        alg('Plate')
        values[p + 'Mode1'] = 1
        values[p + 'Control2'] = reverb_decay_control(raw[0] / 10.0)
    elif kind.startswith('Early Reflection'):
        alg('Early')
    elif kind == 'Stereo Delay':
        alg('StereoDelay')
        time = (raw[0] + raw[1] * 256) / 1000.0
        values[p + 'Control1'] = delay_time_control(time)
        values[p + 'Control2'] = delay_time_control(
            ((raw[4] + raw[5] * 256) or (raw[0] + raw[1] * 256)) / 1000.0)
        values[p + 'Control3'] = delay_feedback_control(raw[2] / 110.0)
    elif kind == 'Cross Delay':
        alg('CrossDelay')
        time = (raw[0] + raw[1] * 256) / 1000.0
        values[p + 'Control1'] = delay_time_control(time)
        values[p + 'Control2'] = delay_time_control(time)
        values[p + 'Control3'] = delay_feedback_control(raw[2] / 110.0)
    elif kind in ('Delay/Hall', 'Delay/Chorus'):
        # A dual-mono M1 effect becomes two slots when slot 3 is free.
        alg('StereoDelay')
        values[p + 'Control1'] = delay_time_control(0.24)
        values[p + 'Control2'] = delay_time_control(0.32)
        values[p + 'Control3'] = delay_feedback_control(0.25)
        if slot == 2 and not extra_slot_state['used']:
            extra_slot_state['used'] = True
            tail = 'Hall' if kind == 'Delay/Hall' else 'Chorus'
            values['fx.fxSlot3Algorithm'] = ALG[tail]
            values['fx.fxSlot3Mix'] = 0.25
    elif kind in ('Stereo Chorus 1', 'Stereo Chorus 2'):
        alg('Chorus')
        values[p + 'Control1'] = chorus_rate_control(0.4)
    elif kind == 'Symphonic Ensemble':
        alg('Ensemble')
    elif kind.startswith('Flanger'):
        alg('Flanger')
    elif kind.startswith('Phaser'):
        alg('Phaser')
    elif kind.startswith('Stereo Tremolo'):
        alg('Tremolo')
    elif kind == 'Rotary Speaker':
        alg('Rotary')
        mix = 1.0
    elif kind == 'Exciter':
        alg('Exciter')
        mix = min(mix, 0.3)
    elif kind == 'Overdrive':
        alg('Overdrive')
    elif kind == 'Distortion':
        alg('Distortion')
    else:
        return f'effect "{kind}" has no R50 algorithm; slot left empty'
    values[p + 'Mix'] = round(mix, 3)
    return None

def convert(number):
    name, sources, extras = SELECTION[number]
    program = PROGRAMS[number]
    gaps = []
    if 'gap' in extras:
        gaps.append(extras['gap'])

    values = {}
    assets = {}
    double = program['oscillator_mode'] == 'Double'
    oscs = ['osc1'] + (['osc2'] if double else [])

    values['patch.voiceMode'] = 1 if program['assign'] == 'Mono' else 0
    if program.get('hold'):
        gaps.append('M1 Hold (infinite sustain) is not modelled')

    for index, osc in enumerate(oscs):
        partial = index + 1
        p = f'partial{partial}.p{partial}'
        params = program[f'oscillator{index + 1}_params']
        source = sources[osc]

        values[p + 'Enabled'] = 1
        if source[0] == 'sample':
            values[p + 'SourceType'] = 1
            values[p + 'SampleInstrument'] = 0   # bootstrap; ID resolves
            assets[str(index)] = source[1]
        else:
            values[p + 'SourceType'] = 0
            values[p + 'Wave'] = source[1]
        if len(source) > 2:                       # breath noise
            values[p + 'NoiseMix'] = source[2]
            values[p + 'NoiseTone'] = 0.65
            values[p + 'NoisePitchTrack'] = 1

        values[p + 'Octave'] = OCTAVE[program[osc]['octave']]
        if osc == 'osc2':
            values[p + 'Semitone'] = program['osc2'].get('interval', 0)
            values[p + 'Fine'] = program['osc2'].get('detune', 0)
        if double:
            values[p + 'Pan'] = -0.22 if partial == 1 else 0.22

        # VDA -> amp EG (the M1 EG maps 1:1 onto R50's extended EG)
        vda = params['vda']
        eg = vda['eg']
        values[p + 'Level'] = level99(vda['oscillator_level'])
        values[p + 'AmpAttack'] = t99(eg['attack_time'])
        values[p + 'AmpAttackLevel'] = l99(eg['attack_level'])
        values[p + 'AmpDecay'] = t99(eg['decay_time'])
        values[p + 'AmpBreak'] = l99(eg['break_point'])
        values[p + 'AmpSlope'] = t99(eg['slope_time'])
        values[p + 'AmpSustain'] = l99(eg['sustain_level'])
        values[p + 'AmpRelease'] = t99(eg['release_time'])
        if vda['amp_velocity_sens'] > 40:
            gaps.append(f'osc{index+1} amp velocity sensitivity '
                        f'({vda["amp_velocity_sens"]}) rides R50\'s fixed '
                        'velocity curve instead of a per-partial amount')

        # VDF -> filter + EG. R50's envelope reaches +/-4 octaves at full
        # amount, the M1's intensity-99 sweep more; anchor the mapping on the
        # audible frequencies instead: solve base cutoff and amount so the
        # sustained position and the envelope peak land where the M1 puts
        # them, and report when the required span exceeds the 4-octave cap.
        vdf = params['vdf']
        feg = vdf['eg']
        intensity = vdf['eg_intensity'] / 99.0
        sus01 = l99(abs(feg['sustain_level']))
        atk01 = l99(abs(feg['attack_level']))
        sustain_hz = cutoff_hz(min(99.0, vdf['cutoff']
                                   + vdf['eg_intensity'] * sus01))
        peak_hz = cutoff_hz(min(99.0, vdf['cutoff']
                                + vdf['eg_intensity'] * atk01))
        if abs(atk01 - sus01) > 0.05 and intensity > 0.01:
            span = math.log2(max(peak_hz, 20) / max(sustain_hz, 20)) \
                 / (atk01 - sus01)
            if abs(span) > 4.0:
                gaps.append(f'osc{index+1} VDF sweep needs '
                            f'{abs(span):.1f} octaves; R50 filter envelope '
                            'caps at 4')
                span = math.copysign(4.0, span)
            amount = round(span / 4.0, 3)
            base = sustain_hz / 2.0 ** (span * sus01)
        else:
            amount = round(intensity, 3)
            base = sustain_hz / 2.0 ** (4.0 * amount * sus01)
        values[p + 'Cutoff'] = round(max(20.0, min(18000.0, base)), 1)
        values[p + 'FilterEnvAmount'] = amount
        values[p + 'KeyTrack'] = round(
            max(0.0, min(1.0, 0.5 + vdf['cutoff_kbd_track'] / 198.0)), 3)
        values[p + 'FilterAttack'] = t99(feg['attack_time'])
        values[p + 'FilterAttackLevel'] = l99(feg['attack_level'])
        values[p + 'FilterDecay'] = t99(feg['decay_time'])
        values[p + 'FilterBreak'] = l99(feg['break_point'])
        values[p + 'FilterSlope'] = t99(feg['slope_time'])
        values[p + 'FilterSustain'] = l99(abs(feg['sustain_level']))
        values[p + 'FilterRelease'] = t99(feg['release_time'])
        if feg['sustain_level'] < 0 or feg.get('release_level', 0) != 0:
            gaps.append(f'osc{index+1} VDF EG uses negative sustain/release '
                        'levels; R50 filter EG is unipolar')
        if vdf['eg_intensity_velocity_sens'] > 30:
            values['mod.mod3Source'] = MOD_SOURCE['velocity']
            values['mod.mod3Dest'] = MOD_DEST['cutoff']
            values['mod.mod3Amount'] = round(
                vdf['eg_intensity_velocity_sens'] / 99.0 * 0.25, 3)

        # Pitch EG: R50's multi-stage pitch envelope carries the M1 shape
        # directly (start -> attack level -> 0 -> release level, in
        # semitones; the M1's +/-99 spans about an octave).
        peg = params.get('pitch_eg', {})
        if any(peg.get(k, 0) for k in
               ('start_level', 'attack_level', 'release_level')):
            values[p + 'PitchStartLevel'] = round(
                peg.get('start_level', 0) / 99.0 * 12.0, 2)
            values[p + 'PitchAmount'] = round(
                peg.get('attack_level', 0) / 99.0 * 12.0, 2)
            values[p + 'PitchAttack'] = t99(peg.get('attack_time', 0), 4.0)
            values[p + 'PitchDecay'] = t99(peg.get('decay_time', 0), 8.0)
            values[p + 'PitchRelease'] = t99(peg.get('release_time', 0), 8.0)
            values[p + 'PitchReleaseLevel'] = round(
                peg.get('release_level', 0) / 99.0 * 12.0, 2)
            if peg.get('time_velocity_sens') or peg.get('level_velocity_sens'):
                gaps.append(f'osc{index+1} pitch EG velocity sensitivity is '
                            'not modelled')

    # Pitch MG -> LFO1 vibrato
    mg = program['pitch_mg']
    if mg['intensity'] > 0:
        values['mod.lfo1Rate'] = mg_hz(mg['frequency'])
        values['mod.lfo1Retrigger'] = 1 if mg['key_sync'] else 0
        values['mod.lfo1Delay'] = round(mg['delay'] / 99.0 * 2.0, 3)
        values['mod.lfo1Fade'] = 0.4 if mg['delay'] else 0.0
        values['mod.mod2Source'] = MOD_SOURCE['lfo1']
        values['mod.mod2Dest'] = MOD_DEST['pitch']
        values['mod.mod2Amount'] = round(mg['intensity'] / 99.0 * 0.03, 4)
    if program['joystick']['pitch_mg_intensity'] > 0:
        gaps.append('joystick-up vibrato (wheel modulating MG depth) has no '
                    'R50 meta-modulation; a wheel-to-cutoff route stands in')

    # The bank's standing playability convention: the wheel always does
    # something useful.
    values['mod.mod1Source'] = MOD_SOURCE['wheel']
    values['mod.mod1Dest'] = MOD_DEST['cutoff']
    values['mod.mod1Amount'] = 0.17

    # Effects
    extra_slot_state = {'used': False}
    for slot, key in ((1, 'effect1'), (2, 'effect2')):
        gap = convert_effect(program['effects'][key], values, slot,
                             extra_slot_state)
        if gap:
            gaps.append(gap)
    if program['effects'].get('placement') == 'Parallel':
        gaps.append('M1 parallel effect placement approximated as serial '
                    'inserts')

    return name, values, assets, gaps

def write_document(index, name, values, assets):
    document = {
        'schemaVersion': 1,
        'name': name,
        'values': values,
        'sampleAssets': assets,
    }
    safe = ''.join(c if c.isalnum() or c in '-_' else '_' for c in name)
    path = os.path.join(OUT, f'{index:03d}_{safe}.json')
    with open(path, 'w') as f:
        json.dump(document, f, indent=2, sort_keys=True)
        f.write('\n')
    return os.path.basename(path)

report = {}
number = 107
for m1_number in sorted(SELECTION):
    name, values, assets, gaps = convert(m1_number)
    filename = write_document(number, name, values, assets)
    report[m1_number] = (name, filename, gaps)
    number += 1

print('== Converted ==')
for m1_number, (name, filename, gaps) in report.items():
    print(f'{m1_number} -> {filename}')
    for gap in dict.fromkeys(gaps):
        print(f'     gap: {gap}')
