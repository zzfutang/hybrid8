//
//  wav_analyze.cpp
//  Spectral / envelope / stereo analysis for recreating a sample as a synth
//  patch. Reads PCM16/24/32 and float32 WAV (skips JUNK etc.), and reports:
//    - format + duration
//    - amplitude envelope (attack / peak / sustain / release character)
//    - fundamental pitch (autocorrelation) + nearest MIDI note
//    - harmonic magnitudes vs. the fundamental (waveform fingerprint)
//    - spectral centroid over time (filter-envelope brightness sweep)
//    - stereo width / detune-beating cues (chorus / unison)
//
//  Build: clang++ -std=c++17 -O2 Tools/wav_analyze.cpp -o /tmp/wav_analyze
//  Run:   /tmp/wav_analyze file.wav
//

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>
#include <complex>
#include <string>
#include <algorithm>

using cd = std::complex<double>;

// ------------------------------------------------------------------ WAV ----
struct Wav { int sr = 0, ch = 0, bits = 0; bool isFloat = false; std::vector<std::vector<float>> chan; };

static uint32_t rd32(const uint8_t* p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t rd16(const uint8_t* p){ return p[0]|(p[1]<<8); }

static bool readWav(const char* path, Wav& w) {
    FILE* f = fopen(path, "rb"); if(!f) return false;
    fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET);
    std::vector<uint8_t> b(n); if(fread(b.data(),1,n,f)!=(size_t)n){fclose(f);return false;} fclose(f);
    if(n<12 || memcmp(b.data(),"RIFF",4) || memcmp(b.data()+8,"WAVE",4)) return false;
    size_t pos=12; int fmt=0; std::vector<uint8_t> data;
    while(pos+8<=(size_t)n){
        const uint8_t* id=b.data()+pos; uint32_t sz=rd32(b.data()+pos+4); pos+=8;
        if(pos+sz>(size_t)n) sz=n-pos;
        if(!memcmp(id,"fmt ",4)){
            fmt=rd16(b.data()+pos); w.ch=rd16(b.data()+pos+2); w.sr=rd32(b.data()+pos+4);
            w.bits=rd16(b.data()+pos+14);
            if(fmt==0xFFFE && sz>=26){ fmt=rd16(b.data()+pos+24); } // extensible subformat
            w.isFloat=(fmt==3);
        } else if(!memcmp(id,"data",4)){
            data.assign(b.data()+pos, b.data()+pos+sz);
        }
        pos += sz + (sz&1); // chunks are word-aligned
    }
    if(w.ch<=0||w.sr<=0||data.empty()) return false;
    int bytes=w.bits/8; size_t frames=data.size()/(bytes*w.ch);
    w.chan.assign(w.ch, std::vector<float>(frames,0.f));
    for(size_t i=0;i<frames;i++) for(int c=0;c<w.ch;c++){
        const uint8_t* p=data.data()+(i*w.ch+c)*bytes; float v=0;
        if(w.isFloat && bytes==4){ float fv; memcpy(&fv,p,4); v=fv; }
        else if(bytes==2){ int16_t s=(int16_t)rd16(p); v=s/32768.f; }
        else if(bytes==3){ int32_t s=(p[0]|(p[1]<<8)|(p[2]<<16)); if(s&0x800000)s|=~0xFFFFFF; v=s/8388608.f; }
        else if(bytes==4){ int32_t s=(int32_t)rd32(p); v=s/2147483648.f; }
        w.chan[c][i]=v;
    }
    return true;
}

// ------------------------------------------------------------------ FFT ----
static void fft(std::vector<cd>& a){
    int n=a.size();
    for(int i=1,j=0;i<n;i++){int bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(int len=2;len<=n;len<<=1){double ang=-2*M_PI/len;cd wl(cos(ang),sin(ang));
        for(int i=0;i<n;i+=len){cd w(1,0);for(int k=0;k<len/2;k++){cd u=a[i+k],v=a[i+k+len/2]*w;a[i+k]=u+v;a[i+k+len/2]=u-v;w*=wl;}}}
}
static int nextpow2(int x){int p=1;while(p<x)p<<=1;return p;}

// magnitude spectrum of a windowed segment (Hann)
static std::vector<double> spectrum(const std::vector<float>& x,int a,int len,double& binHz,double sr){
    int N=nextpow2(len); std::vector<cd> buf(N,cd(0,0));
    for(int i=0;i<len && a+i<(int)x.size();i++){double win=0.5-0.5*cos(2*M_PI*i/(len-1));buf[i]=cd(x[a+i]*win,0);}
    fft(buf); binHz=sr/N; std::vector<double> mag(N/2); for(int k=0;k<N/2;k++)mag[k]=std::abs(buf[k]);
    return mag;
}

// autocorrelation fundamental in [fmin,fmax]
static double fundamental(const std::vector<float>& x,int a,int len,double sr,double fmin,double fmax){
    int lagMin=(int)(sr/fmax), lagMax=std::min(len-1,(int)(sr/fmin));
    double best=0; int bestLag=0;
    double norm=0; for(int i=0;i<len&&a+i<(int)x.size();i++)norm+=x[a+i]*x[a+i];
    if(norm<1e-9)return 0;
    for(int lag=lagMin;lag<=lagMax;lag++){ double s=0; for(int i=0;i+lag<len&&a+i+lag<(int)x.size();i++)s+=x[a+i]*x[a+i+lag];
        if(s>best){best=s;bestLag=lag;} }
    return bestLag>0? sr/bestLag : 0;
}
static const char* NOTES[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static std::string noteName(double hz){ if(hz<=0)return "?"; double m=69+12*log2(hz/440.0); int mi=(int)lround(m); char buf[16]; snprintf(buf,16,"%s%d(%+.0fc)",NOTES[((mi%12)+12)%12],mi/12-1, (m-mi)*100); return buf; }

static double rms(const std::vector<float>& x,int a,int b){double s=0;int n=0;for(int i=a;i<b&&i<(int)x.size();i++){s+=x[i]*x[i];n++;}return n?sqrt(s/n):0;}

int main(int argc,char**argv){
    if(argc<2){printf("usage: %s file.wav\n",argv[0]);return 1;}
    Wav w; if(!readWav(argv[1],w)){printf("failed to read %s\n",argv[1]);return 1;}
    double sr=w.sr; int frames=w.chan[0].size(); double dur=frames/sr;
    printf("== %s ==\n",argv[1]);
    printf("format: %d Hz, %d ch, %d-bit %s, %.3f s (%d frames)\n",
           w.sr,w.ch,w.bits,w.isFloat?"float":"int",dur,frames);

    // mono mix
    std::vector<float> mono(frames,0.f);
    for(int i=0;i<frames;i++){double s=0;for(int c=0;c<w.ch;c++)s+=w.chan[c][i];mono[i]=s/w.ch;}

    // --- amplitude envelope (10 ms RMS windows) ---
    int win=(int)(sr*0.01); std::vector<double> env; for(int a=0;a+win<frames;a+=win)env.push_back(rms(mono,a,a+win));
    double peak=0; int peakIdx=0; for(size_t i=0;i<env.size();i++)if(env[i]>peak){peak=env[i];peakIdx=(int)i;}
    // attack = time to reach 90% of peak; find sustain (avg of middle third), end decay
    int atk=0; for(size_t i=0;i<env.size();i++){if(env[i]>=0.9*peak){atk=i;break;}}
    double sus= env.size()>6 ? [&]{double s=0;int n=0;for(size_t i=env.size()/2;i<env.size()*3/4;i++){s+=env[i];n++;}return n?s/n:0;}() : 0;
    // release: from note-off-ish (last quarter) does it fall?
    double tailStart=env.size()? env[env.size()*3/4] : 0, tailEnd=env.size()? env.back():0;
    printf("envelope: peak@%.0fms  attack~%.0fms  sustain/peak=%.2f  tail %.2f->%.2f (%s)\n",
           peakIdx*10.0, atk*10.0, peak>0?sus/peak:0, tailStart, tailEnd,
           (sus/peak>0.5?"sustained":"decaying"));

    // --- fundamental (from a stable window ~25% in) ---
    int fa=(int)std::min((double)frames*0.25, dur*sr*0.25); int flen=std::min(frames-fa,(int)(sr*0.09));
    double f0=fundamental(mono,fa,flen,sr,40,3000);
    printf("fundamental: %.1f Hz  = %s\n",f0,noteName(f0).c_str());

    // --- harmonic fingerprint (long window near sustain) ---
    int sa=fa; int slen=std::min(frames-sa,(int)(sr*0.35)); double binHz; auto mag=spectrum(mono,sa,slen,binHz,sr);
    auto peakNear=[&](double f){int k0=(int)(f/binHz); double m=0; for(int k=std::max(1,k0-2);k<=k0+2&&k<(int)mag.size();k++)m=std::max(m,mag[k]); return m;};
    double h1=peakNear(f0); printf("harmonics (vs fundamental):\n");
    if(h1>0){ double oddSum=0,evenSum=0; for(int k=1;k<=16;k++){double m=peakNear(f0*k)/h1;
        printf("  h%-2d %6.1fHz  %5.2f %s\n",k,f0*k,m,std::string((int)std::min(40.0,m*40),'#').c_str());
        if(k>1){(k%2? oddSum:evenSum)+=m;} }
        printf("  odd/even harmonic energy = %.2f (%s)\n", evenSum>0?oddSum/evenSum:99,
               evenSum>0 && oddSum/evenSum>3 ? "square/pulse-ish (weak evens)":"saw-ish (full series)");
    }
    // spectral rolloff: highest harmonic above 5% of fundamental -> filter brightness
    int topH=1; for(int k=1;k<=60;k++) if(peakNear(f0*k)/std::max(h1,1e-9)>0.05) topH=k;
    printf("brightness: energy up to ~h%d (~%.0f Hz) => filter cutoff region\n", topH, f0*topH);

    // --- spectral centroid over time (filter-envelope sweep) ---
    printf("spectral centroid over time (brightness sweep):\n");
    int seg=(int)(sr*0.05); int nseg=std::min(12,(frames-fa)/seg);
    for(int s=0;s<nseg;s++){ double bh; auto m=spectrum(mono,fa+s*seg,seg,bh,sr); double num=0,den=0;
        for(int k=1;k<(int)m.size();k++){num+=k*bh*m[k];den+=m[k];} double cen=den>0?num/den:0;
        printf("  t=%4.0fms  centroid=%6.0f Hz  %s\n", fa/sr*1000+s*50.0, cen, std::string((int)std::min(40.0,cen/300),'=').c_str()); }

    // --- stereo width / detune beating ---
    if(w.ch>=2){ double lr=0,ll=0,rr=0; for(int i=0;i<frames;i++){lr+=w.chan[0][i]*w.chan[1][i];ll+=w.chan[0][i]*w.chan[0][i];rr+=w.chan[1][i]*w.chan[1][i];}
        double corr= (ll>0&&rr>0)? lr/sqrt(ll*rr):1; printf("stereo: L/R correlation=%.3f (%s)\n",corr, corr<0.85?"wide (chorus/detune/unison)":corr<0.98?"mild width":"near-mono"); }
    // beating: amplitude modulation rate of the fundamental (slow AM => detune/chorus)
    // measure envelope ripple period in the sustain
    { std::vector<double> e2; int w2=(int)(sr*0.005); for(int a=fa;a+w2<fa+std::min(frames-fa,(int)(sr*0.5));a+=w2)e2.push_back(rms(mono,a,a+w2));
      double mean=0; for(double v:e2)mean+=v; mean/= std::max((size_t)1,e2.size());
      int zc=0; for(size_t i=1;i<e2.size();i++) if((e2[i-1]-mean)*(e2[i]-mean)<0)zc++;
      double amHz = e2.size()? zc/2.0 / (e2.size()*0.005) : 0;
      printf("amplitude ripple ~%.1f Hz (detune/chorus beat or LFO)\n", amHz); }
    return 0;
}
