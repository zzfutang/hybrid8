//
//  synth_match.cpp
//  Grid-searches synth parameters to best match a target harmonic spectrum
//  (extracted from a sample by wav_analyze). Renders each candidate through the
//  real SynthEngine, measures harmonic magnitudes at the same analysis window,
//  and reports the lowest-error parameter set.
//
//  Build: clang++ -std=c++17 -O2 -I Extension Tools/synth_match.cpp -o /tmp/synth_match
//  Run:   /tmp/synth_match <keys|juno>
//

#include "../Products/Hybrid8/DSP/Hybrid8Engine.hpp"
#include <vector>
#include <complex>
#include <cmath>
#include <cstdio>
#include <string>
using namespace synth;
using cd = std::complex<double>;

static void fft(std::vector<cd>& a){int n=a.size();for(int i=1,j=0;i<n;i++){int b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j)std::swap(a[i],a[j]);}for(int L=2;L<=n;L<<=1){double an=-2*M_PI/L;cd wl(cos(an),sin(an));for(int i=0;i<n;i+=L){cd w(1,0);for(int k=0;k<L/2;k++){cd u=a[i+k],v=a[i+k+L/2]*w;a[i+k]=u+v;a[i+k+L/2]=u-v;w*=wl;}}}}

// harmonic magnitudes h1..H of a rendered note (mono), normalised to h1.
static void harmonics(SynthEngine& e,double f0,double sr,int H,std::vector<double>& out){
    int N=(int)(sr*0.75); std::vector<float> L(N),R(N); e.noteOn(48,100); e.render(L.data(),R.data(),N);
    int a=(int)(sr*0.33), len=(int)(sr*0.4); int NF=1;while(NF<len)NF<<=1;
    std::vector<cd> buf(NF,cd(0,0));
    for(int i=0;i<len;i++){double win=0.5-0.5*cos(2*M_PI*i/(len-1));buf[i]=cd(L[a+i]*win,0);}
    fft(buf); double binHz=sr/NF;
    auto peak=[&](double f){int k0=(int)(f/binHz);double m=0;for(int k=std::max(1,k0-2);k<=k0+2&&k<NF/2;k++)m=std::max(m,std::abs(buf[k]));return m;};
    double h1=peak(f0); out.assign(H+1,0); for(int k=1;k<=H;k++) out[k]= h1>0? peak(f0*k)/h1 : 0;
}

int main(int argc,char**argv){
    std::string which=argc>1?argv[1]:"keys";
    double sr=48000, f0=130.81; int H=8;
    // targets measured from the samples (h1..h8)
    std::vector<double> tgt = which=="juno"
        ? std::vector<double>{0, 1.00,1.06,0.40,0.64,0.23,0.25,0.07,0.09}
        : std::vector<double>{0, 1.00,0.66,0.32,0.54,0.14,0.35,0.07,0.19};

    double bestErr=1e9; std::string best;
    // grid
    for(int o2oct : {0,1})
    for(double o1 : {0.5,0.6,0.7})
    for(double o2 : {0.0,0.2,0.35,0.5,0.65})
    for(double cut : {260.0,360.0,460.0,560.0,700.0,900.0,1300.0})
    for(double res : {0.05,0.22,0.38,0.54,0.68})
    for(int slope : {0,1})
    {
        SynthEngine e; e.setSampleRate(sr);
        e.setParameter(SynthParamOsc1Level,(float)o1); e.setParameter(SynthParamOsc2Level,(float)o2);
        e.setParameter(SynthParamOsc2Octave,(float)o2oct);
        e.setParameter(SynthParamFilterSlope,(float)slope);
        e.setParameter(SynthParamFilterCutoff,(float)cut);
        e.setParameter(SynthParamFilterResonance,(float)res);
        e.setParameter(SynthParamFilterEnvAmount,0.1f);           // near-static cutoff at window
        e.setParameter(SynthParamFilterDecay,normFromTime(1.2f));
        e.setParameter(SynthParamFilterSustain,0.6f);
        e.setParameter(SynthParamAmpSustain,1.0f); e.setParameter(SynthParamAmpAttack,normFromTime(0.002f));
        e.setParameter(SynthParamAnalogAmount,0.15f);
        std::vector<double> h; harmonics(e,f0,sr,H,h);
        double err=0; for(int k=2;k<=H;k++){double d=h[k]-tgt[k]; err+=d*d;}
        if(err<bestErr){ bestErr=err; char b[256];
            snprintf(b,256,"o2oct=%d o1=%.2f o2=%.2f cut=%.0f res=%.2f slope=%d  -> h: %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f (err=%.3f)",
                o2oct,o1,o2,cut,res,slope,h[1],h[2],h[3],h[4],h[5],h[6],h[7],h[8],err); best=b; }
    }
    printf("target %-5s h:            %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",which.c_str(),
           tgt[1],tgt[2],tgt[3],tgt[4],tgt[5],tgt[6],tgt[7],tgt[8]);
    printf("BEST   %s\n", best.c_str());
    return 0;
}
