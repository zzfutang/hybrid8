//
//  synth_match2.cpp
//  Spectral-distance sound matching (à la InverSynth / DDSP sound-matching, but
//  with a plain search instead of a neural net). Renders candidate patches
//  through the real SynthEngine and minimises a TIME-SEGMENTED log-magnitude
//  spectrogram distance to the target sample — so the envelope (filter closing,
//  amp decay) is matched, not just one steady-state frame. Optimises by
//  coordinate descent over a discrete grid per parameter.
//
//  Build: clang++ -std=c++17 -O2 -I Extension Tools/synth_match2.cpp -o /tmp/synth_match2
//  Run:   /tmp/synth_match2 target.wav [gateSec] [note]   (writes best -> /tmp/out.wav)
//

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"
#include <vector>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <algorithm>
using namespace synth;
using cd = std::complex<double>;

// ---- WAV read (PCM16/24/32 + float32, skips JUNK) --------------------------
static uint32_t rd32(const uint8_t*p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t rd16(const uint8_t*p){return p[0]|(p[1]<<8);}
static bool readWavMono(const char*path,std::vector<float>&mono,int&sr){
    FILE*f=fopen(path,"rb");if(!f)return false;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    std::vector<uint8_t>b(n);if(fread(b.data(),1,n,f)!=(size_t)n){fclose(f);return false;}fclose(f);
    if(n<12||memcmp(b.data(),"RIFF",4)||memcmp(b.data()+8,"WAVE",4))return false;
    size_t pos=12;int fmt=0,ch=0,bits=0;std::vector<uint8_t>data;
    while(pos+8<=(size_t)n){const uint8_t*id=b.data()+pos;uint32_t sz=rd32(b.data()+pos+4);pos+=8;if(pos+sz>(size_t)n)sz=n-pos;
        if(!memcmp(id,"fmt ",4)){fmt=rd16(b.data()+pos);ch=rd16(b.data()+pos+2);sr=rd32(b.data()+pos+4);bits=rd16(b.data()+pos+14);if(fmt==0xFFFE&&sz>=26)fmt=rd16(b.data()+pos+24);}
        else if(!memcmp(id,"data",4))data.assign(b.data()+pos,b.data()+pos+sz);
        pos+=sz+(sz&1);}
    if(ch<=0||sr<=0||data.empty())return false;int by=bits/8;size_t fr=data.size()/(by*ch);mono.assign(fr,0);
    for(size_t i=0;i<fr;i++){double s=0;for(int c=0;c<ch;c++){const uint8_t*p=data.data()+(i*ch+c)*by;float v=0;
        if(fmt==3&&by==4){float fv;memcpy(&fv,p,4);v=fv;}else if(by==2){int16_t q=(int16_t)rd16(p);v=q/32768.f;}
        else if(by==3){int32_t q=(p[0]|(p[1]<<8)|(p[2]<<16));if(q&0x800000)q|=~0xFFFFFF;v=q/8388608.f;}
        else if(by==4){int32_t q=(int32_t)rd32(p);v=q/2147483648.f;} s+=v;} mono[i]=s/ch;}
    return true;
}
static void fft(std::vector<cd>&a){int n=a.size();for(int i=1,j=0;i<n;i++){int b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j)std::swap(a[i],a[j]);}for(int L=2;L<=n;L<<=1){double an=-2*M_PI/L;cd wl(cos(an),sin(an));for(int i=0;i<n;i+=L){cd w(1,0);for(int k=0;k<L/2;k++){cd u=a[i+k],v=a[i+k+L/2]*w;a[i+k]=u+v;a[i+k+L/2]=u-v;w*=wl;}}}}

// Time-segmented log-magnitude spectrogram: F frames x B log-freq bands,
// L2-normalised over the whole thing (keeps envelope + spectral shape, drops
// absolute gain). ~50 Hz..12 kHz.
static const int F=16, B=44, WIN=4096;
static std::vector<double> spec(const std::vector<float>&x,int sr,double dur){
    int total=(int)(dur*sr); std::vector<double> S(F*B,0.0);
    double fmin=60, fmax=9000, lr=log(fmax/fmin);
    for(int fr=0;fr<F;fr++){
        int start=(int)((double)fr/(F)* (total-WIN)); if(start<0)start=0;
        std::vector<cd> buf(WIN); for(int i=0;i<WIN;i++){double w=0.5-0.5*cos(2*M_PI*i/(WIN-1));double s=(start+i<(int)x.size())?x[start+i]:0;buf[i]=cd(s*w,0);} fft(buf);
        double bh=(double)sr/WIN;
        for(int k=1;k<WIN/2;k++){double f=k*bh; if(f<fmin||f>fmax)continue; int band=(int)(log(f/fmin)/lr*B); if(band<0||band>=B)continue; S[fr*B+band]+=std::abs(buf[k]);}
        for(int band=0;band<B;band++) S[fr*B+band]=log(1.0+40.0*S[fr*B+band]);
    }
    double e=0; for(double v:S)e+=v*v; e=sqrt(e)+1e-9; for(double&v:S)v/=e; return S;
}
static double dist(const std::vector<double>&a,const std::vector<double>&b){double s=0;for(size_t i=0;i<a.size();i++)s+=std::fabs(a[i]-b[i]);return s;}

// ---- searchable parameters -------------------------------------------------
struct Par { const char* name; SynthParam id; bool isTime; std::vector<float> vals; int cur; };
static double sr=48000; static double gate=2.0; static int NOTE=48; static double DUR=2.0;
static std::vector<double> TARGET;

static std::vector<double> renderSpec(std::vector<Par>&P){
    SynthEngine e; e.setSampleRate(sr);
    e.setParameter(SynthParamAmpSustain,1.0f);
    for(auto&p:P) e.setParameter(p.id, p.isTime? normFromTime(p.vals[p.cur]) : p.vals[p.cur]);
    int N=(int)(sr*DUR); std::vector<float>L(N),R(N); int gN=std::min((int)(gate*sr),N);
    e.noteOn(NOTE,100); e.render(L.data(),R.data(),gN); if(gN<N){e.noteOff(NOTE);e.render(L.data()+gN,R.data()+gN,N-gN);}
    return spec(L,(int)sr,DUR);
}
static double loss(std::vector<Par>&P){ return dist(renderSpec(P),TARGET); }

int main(int argc,char**argv){
    if(argc<2){printf("usage: %s target.wav [gate] [note]\n",argv[0]);return 1;}
    std::vector<float> tgt; int tsr; if(!readWavMono(argv[1],tgt,tsr)){printf("read fail\n");return 1;} sr=tsr;
    DUR=std::min(2.0,(double)tgt.size()/sr); gate=argc>2?atof(argv[2]):DUR; NOTE=argc>3?atoi(argv[3]):48;
    TARGET=spec(tgt,(int)sr,DUR);

    std::vector<Par> P = {
        {"o1w",  SynthParamOscWaveform, false, {0}, 0},           // saw family (from analysis)
        {"o2w",  SynthParamOsc2Waveform,false, {0}, 0},
        {"o2oct",SynthParamOsc2Octave,  false, {0,1,2}, 1},        // osc2 +0/+12/+24
        {"o2det",SynthParamOsc2Detune,  false, {0,4,8}, 0},
        {"o1lvl",SynthParamOsc1Level,   false, {0.45f,0.55f,0.65f,0.75f}, 2},
        {"o2lvl",SynthParamOsc2Level,   false, {0.0f,0.2f,0.35f,0.5f,0.65f}, 3},
        {"slope",SynthParamFilterSlope, false, {0,1}, 1},
        {"cut",  SynthParamFilterCutoff,false, {450,520,560,620,700,850,1100,1500}, 2},
        {"res",  SynthParamFilterResonance,false,{0.1f,0.25f,0.35f,0.45f,0.6f}, 2},
        {"drive",SynthParamFilterDrive, false, {0.0f,0.2f}, 0},
        {"fenv", SynthParamFilterEnvAmount,false,{0.0f,0.15f,0.3f,0.5f}, 2},
        {"fatk", SynthParamFilterAttack,true,  {0.002f,0.02f,0.06f}, 0},
        {"fdec", SynthParamFilterDecay, true,  {0.3f,0.6f,1.0f}, 1},
        {"fsus", SynthParamFilterSustain,false,{0.0f,0.2f,0.4f,0.6f}, 1},
        {"aatk", SynthParamAmpAttack,   true,  {0.002f,0.02f,0.045f,0.1f}, 0},
        {"adec", SynthParamAmpDecay,    true,  {0.3f,0.6f,0.9f,1.4f}, 2},
        {"asus", SynthParamAmpSustain,  false, {0.0f,0.2f,0.35f,0.6f}, 1},
        {"arel", SynthParamAmpRelease,  true,  {0.2f,0.5f,0.9f}, 1},
        {"analog",SynthParamAnalogAmount,false,{0.2f,0.35f}, 0},
    };

    double best=loss(P);
    for(int pass=0;pass<4;pass++){ bool improved=false;
        for(auto&p:P){ int bi=p.cur; double bl=best;
            for(int i=0;i<(int)p.vals.size();i++){ p.cur=i; double l=loss(P); if(l<bl){bl=l;bi=i;improved=true;} }
            p.cur=bi; best=bl; }
        if(!improved) break;
    }
    // write best render
    { SynthEngine e; e.setSampleRate(sr); e.setParameter(SynthParamAmpSustain,1.0f);
      for(auto&p:P) e.setParameter(p.id,p.isTime?normFromTime(p.vals[p.cur]):p.vals[p.cur]);
      int N=(int)(sr*DUR); std::vector<float>L(N),R(N); int gN=std::min((int)(gate*sr),N);
      e.noteOn(NOTE,100); e.render(L.data(),R.data(),gN); if(gN<N){e.noteOff(NOTE);e.render(L.data()+gN,R.data()+gN,N-gN);}
      FILE*f=fopen("/tmp/out.wav","wb");int data=N*4,riff=36+data;auto w32=[&](uint32_t v){fwrite(&v,4,1,f);};auto w16=[&](uint16_t v){fwrite(&v,2,1,f);};
      fwrite("RIFF",1,4,f);w32(riff);fwrite("WAVE",1,4,f);fwrite("fmt ",1,4,f);w32(16);w16(1);w16(2);w32((int)sr);w32((int)sr*4);w16(4);w16(16);fwrite("data",1,4,f);w32(data);
      for(int i=0;i<N;i++){int s=(int)lround(std::max(-1.f,std::min(1.f,L[i]))*32767);w16((int16_t)s);w16((int16_t)s);}fclose(f); }

    printf("== match %s  (note %d, gate %.2fs, loss %.4f) ==\n",argv[1],NOTE,gate,best);
    for(auto&p:P) printf("  %-7s = %g\n", p.name, p.vals[p.cur]);
    return 0;
}
