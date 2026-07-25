//
//  wt_extract.cpp
//  Wavetable extractor: reads a recorded sample (e.g. piano.wav), detects the
//  pitch, and samples WT_NUM_FRAMES single-cycle "frames" across the note's
//  evolution (attack -> decay). Each frame is turned into a harmonic-magnitude
//  spectrum (the synth band-limits it into a mip pyramid at load time), and the
//  whole thing is emitted as a C++ header so it becomes a BUILT-IN wavetable set.
//
//  Build: clang++ -std=c++17 -O2 Tools/wt_extract.cpp -o /tmp/wt_extract
//  Run:   /tmp/wt_extract piano.wav Piano > Extension/DSP/WavetablePianoData.hpp
//

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <complex>
#include <cmath>
#include <string>
#include <algorithm>
using cd = std::complex<double>;

static const int FRAMES = 32;    // must equal WT_NUM_FRAMES

// ---- WAV read (mono mix; PCM16/24/32 + float32, skips JUNK) -----------------
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
static double rms(const std::vector<float>&x,int a,int b){double s=0;int n=0;for(int i=a;i<b&&i<(int)x.size();i++){s+=x[i]*x[i];n++;}return n?sqrt(s/n):0;}

// autocorrelation fundamental in [fmin,fmax]
static double fundamental(const std::vector<float>&x,int a,int len,double sr,double fmin,double fmax){
    int lagMin=(int)(sr/fmax),lagMax=std::min(len-1,(int)(sr/fmin));double best=0;int bl=0;
    for(int lag=lagMin;lag<=lagMax;lag++){double s=0;for(int i=0;i+lag<len&&a+i+lag<(int)x.size();i++)s+=x[a+i]*x[a+i+lag];if(s>best){best=s;bl=lag;}}
    return bl?sr/bl:0;
}

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"usage: %s sample.wav [Name] > out.hpp\n",argv[0]);return 1;}
    std::string name = argc>2? argv[2] : "Piano";
    std::vector<float> x; int sr; if(!readWavMono(argv[1],x,sr)){fprintf(stderr,"read fail\n");return 1;}
    int frames=x.size();

    // energy span of the note
    double pk=0; for(int i=0;i+240<frames;i+=240) pk=std::max(pk,rms(x,i,i+240));
    int start=0,end=frames-1;
    for(int i=0;i+240<frames;i+=240){ if(rms(x,i,i+240)>0.04*pk){start=i;break;} }
    for(int i=frames-240;i>0;i-=240){ if(rms(x,i,i+240)>0.02*pk){end=i;break;} }
    int span=std::max(1,end-start);

    // pitch from a stable window ~20% into the span
    int fa=start+(int)(span*0.2), flen=std::min(frames-fa,(int)(sr*0.09));
    double f0=fundamental(x,fa,flen,sr,40,3000);
    fprintf(stderr,"%s: sr=%d f0=%.1f Hz  note-span %.2f..%.2f s\n",argv[1],sr,f0,start/(double)sr,end/(double)sr);
    if(f0<20){fprintf(stderr,"no pitch found\n");return 1;}

    int maxH = std::min(500, (int)std::floor(0.45*sr/f0));  // harmonics below Nyquist
    int WIN=4096; // ~85 ms analysis window per frame

    // extract FRAMES spectra across the note (5%..90% of the span)
    std::vector<std::vector<float>> mag(FRAMES, std::vector<float>(maxH+1,0.f));
    for(int fi=0; fi<FRAMES; ++fi){
        double pos = start + span*(0.05 + 0.85*fi/(FRAMES-1));
        int a=(int)pos - WIN/2; if(a<0)a=0; if(a+WIN>frames)a=frames-WIN; if(a<0)a=0;
        std::vector<cd> buf(WIN,cd(0,0));
        for(int i=0;i<WIN && a+i<frames;i++){double w=0.5-0.5*cos(2*M_PI*i/(WIN-1)); buf[i]=cd(x[a+i]*w,0);}
        fft(buf); double binHz=(double)sr/WIN;
        double peak=0;
        for(int k=1;k<=maxH;k++){ int k0=(int)(k*f0/binHz); double m=0;
            for(int b=std::max(1,k0-1); b<=k0+1 && b<WIN/2; ++b) m=std::max(m,std::abs(buf[b]));
            mag[fi][k]=(float)m; peak=std::max(peak,m); }
        // normalise each frame to unit peak (equal loudness across the morph)
        if(peak>1e-9) for(int k=1;k<=maxH;k++) mag[fi][k]/=peak;
    }

    // ---- emit header ----
    std::string data = name + "Data";
    printf("//\n//  Wavetable%s.hpp  (generated by Tools/wt_extract.cpp from %s)\n//\n", data.c_str(), argv[1]);
    printf("//  %d frames x %d harmonics, extracted across the note's evolution.\n//\n\n", FRAMES, maxH);
    printf("#pragma once\nnamespace synth {\n");
    printf("static constexpr int kWt%sFrames = %d;\n", name.c_str(), FRAMES);
    printf("static constexpr int kWt%sMaxH   = %d;\n", name.c_str(), maxH);
    printf("static const float kWt%sMag[kWt%sFrames][kWt%sMaxH + 1] = {\n", name.c_str(), name.c_str(), name.c_str());
    for(int fi=0; fi<FRAMES; ++fi){
        printf("  {0"); for(int k=1;k<=maxH;k++) printf(",%.5ff", mag[fi][k]); printf("},\n");
    }
    printf("};\n} // namespace synth\n");
    return 0;
}
