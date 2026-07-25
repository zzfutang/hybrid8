//
//  synth_render.cpp
//  Renders a hard-coded patch through the real SynthEngine to a WAV, so it can
//  be spectrally compared (via wav_analyze) against a target sample when
//  recreating a sound. Note-on at t=0, note-off at `gate` seconds, 2 s total.
//
//  Build: clang++ -std=c++17 -O2 -I Extension Tools/synth_render.cpp -o /tmp/synth_render
//  Run:   /tmp/synth_render <keys|juno> [midiNote] [gateSec]   (writes /tmp/out.wav)
//

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace synth;

struct P { SynthParam id; float v; bool isTime; };

// ---- Patches under construction (edit + recompile to iterate) --------------
static std::vector<P> keysPatch() {
    return {
        {SynthParamOscWaveform,0,false}, {SynthParamOsc2Waveform,0,false},
        {SynthParamOsc2Octave,1,false},            // osc2 one octave up -> strong evens
        {SynthParamOsc1Level,0.6f,false}, {SynthParamOsc2Level,0.5f,false},
        {SynthParamFilterSlope,0,false}, {SynthParamFilterCutoff,900,false},
        {SynthParamFilterResonance,0.10f,false}, {SynthParamFilterEnvAmount,0.45f,false},
        {SynthParamFilterAttack,0.002f,true}, {SynthParamFilterDecay,0.55f,true},
        {SynthParamFilterSustain,0.0f,false},
        {SynthParamAmpAttack,0.002f,true}, {SynthParamAmpDecay,0.9f,true},
        {SynthParamAmpSustain,0.0f,false}, {SynthParamAmpRelease,0.25f,true},
        {SynthParamAnalogAmount,0.25f,false}, {SynthParamMasterGain,0.8f,false},
    };
}
static std::vector<P> junoPatch() {
    return {
        {SynthParamOscWaveform,0,false}, {SynthParamOsc2Waveform,0,false},
        {SynthParamOsc2Octave,1,false},
        {SynthParamOsc1Level,0.6f,false}, {SynthParamOsc2Level,0.55f,false},
        {SynthParamFilterSlope,0,false}, {SynthParamFilterCutoff,700,false},
        {SynthParamFilterResonance,0.20f,false}, {SynthParamFilterEnvAmount,0.25f,false},
        {SynthParamFilterAttack,0.04f,true}, {SynthParamFilterDecay,0.8f,true},
        {SynthParamFilterSustain,0.5f,false},
        {SynthParamAmpAttack,0.04f,true}, {SynthParamAmpDecay,0.8f,true},
        {SynthParamAmpSustain,0.4f,false}, {SynthParamAmpRelease,0.7f,true},
        {SynthParamAnalogAmount,0.4f,false}, {SynthParamOscPhaseSpread,0.7f,false},
        {SynthParamMasterGain,0.75f,false},
    };
}

static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
    FILE* f=fopen(path,"wb"); int n=L.size(); int data=n*2*2; int riff=36+data;
    auto w32=[&](uint32_t v){fwrite(&v,4,1,f);}; auto w16=[&](uint16_t v){fwrite(&v,2,1,f);};
    fwrite("RIFF",1,4,f); w32(riff); fwrite("WAVE",1,4,f); fwrite("fmt ",1,4,f);
    w32(16); w16(1); w16(2); w32(sr); w32(sr*4); w16(4); w16(16); fwrite("data",1,4,f); w32(data);
    for(int i=0;i<n;i++){ auto c=[&](float x){int s=(int)lround(std::max(-1.f,std::min(1.f,x))*32767); w16((int16_t)s);}; c(L[i]); c(R[i]); }
    fclose(f);
}

int main(int argc,char**argv){
    std::string which = argc>1? argv[1] : "keys";
    int note = argc>2? atoi(argv[2]) : 48;         // C3
    double gate = argc>3? atof(argv[3]) : (which=="keys"? 2.0 : 1.0);
    double sr=48000; int N=(int)(sr*2.0);
    SynthEngine e; e.setSampleRate(sr);
    auto patch = which=="juno"? junoPatch() : keysPatch();
    for(auto& p : patch) e.setParameter(p.id, p.isTime? normFromTime(p.v) : p.v);

    // key=value overrides (e.g. cut=1200 res=0.12 env=0.35 fdec=0.9 fsus=0.2
    //                            o1=0.6 o2=0.42 o2oct=1 drive=0 slope=0)
    struct O { const char* k; SynthParam id; bool t; };
    O ov[]={{"cut",SynthParamFilterCutoff,false},{"res",SynthParamFilterResonance,false},
            {"env",SynthParamFilterEnvAmount,false},{"fatk",SynthParamFilterAttack,true},
            {"fdec",SynthParamFilterDecay,true},{"fsus",SynthParamFilterSustain,false},
            {"o1",SynthParamOsc1Level,false},{"o2",SynthParamOsc2Level,false},
            {"o2oct",SynthParamOsc2Octave,false},{"o2semi",SynthParamOsc2Semitone,false},
            {"o2det",SynthParamOsc2Detune,false},{"drive",SynthParamFilterDrive,false},
            {"slope",SynthParamFilterSlope,false},{"adec",SynthParamAmpDecay,true},
            {"asus",SynthParamAmpSustain,false},{"gain",SynthParamMasterGain,false},
            {"xmod",SynthParamOscCrossMod,false},{"tz",SynthParamOscCrossModTZ,false},
            {"o2w",SynthParamOsc2Waveform,false},{"o1w",SynthParamOscWaveform,false}};
    for(int i=4;i<argc;i++){ char k[32]; float v; if(sscanf(argv[i],"%31[^=]=%f",k,&v)==2)
        for(auto& o:ov) if(!strcmp(k,o.k)) e.setParameter(o.id, o.t? normFromTime(v):v); }
    std::vector<float> L(N),R(N);
    int gateN=(int)(gate*sr);
    e.noteOn(note,100);
    int done=0;
    // render in two chunks so we can insert the note-off at `gate`
    e.render(L.data(), R.data(), std::min(gateN,N)); done=std::min(gateN,N);
    if(done<N){ e.noteOff(note); e.render(L.data()+done, R.data()+done, N-done); }
    writeWav("/tmp/out.wav", L, R, (int)sr);
    float pk=0; for(float s:L)pk=std::max(pk,std::fabs(s));
    printf("rendered %s note=%d gate=%.2fs -> /tmp/out.wav  peak=%.3f\n", which.c_str(), note, gate, pk);
    return 0;
}
