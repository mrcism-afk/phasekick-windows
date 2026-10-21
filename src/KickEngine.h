#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace phasekick {
constexpr double pi=3.14159265358979323846;
enum Param {Tune,Pitch,PitchDecay,PitchCurve,Length,Attack,Hold,Sustain,DecayCurve,Transient,ClickTone,ClickLength,Noise,NoiseDecay,NoiseTone,Grit,Fold,Sub,Punch,Phase,Velocity,Output,Preset,Trigger,NumParams};
struct Spec {const char* name;const char* unit;double min,max,initial,skew;};
inline constexpr std::array<Spec,Preset> specs{{
{"Tune","Hz",25,100,49,1},{"Pitch sweep","st",0,72,34,1},{"Pitch decay","ms",2,250,40,2},{"Pitch curve","",.3,4,1.6,1},
{"Length","ms",60,2000,480,2},{"Attack","ms",.1,20,.5,2},{"Hold","ms",0,120,8,2},{"Sustain","%",0,100,18,1},
{"Decay curve","",.35,4,1.5,1},{"Transient","%",0,100,25,1},{"Click tone","Hz",200,12000,3200,2},{"Click length","ms",.5,35,4,2},
{"Noise","%",0,100,8,1},{"Noise decay","ms",2,400,35,2},{"Noise tone","Hz",100,16000,6800,2},{"Grit","dB",0,30,3,1},
{"Fold","%",0,100,0,1},{"Sub","%",0,100,12,1},{"Punch","%",0,100,20,1},{"Phase","deg",0,360,0,1},
{"Velocity","%",0,100,65,1},{"Output","dB",-30,0,-6,1}}};
using Values=std::array<double,Preset>;
inline Values defaults(){Values v{};for(int i=0;i<Preset;++i)v[i]=specs[i].initial;return v;}
inline constexpr std::array<const char*,8> presetNames{{"Deep Foundation","Warehouse 909","Short Circuit","Sub Pressure","Industrial Heat","Broken Concrete","808 Afterglow","Minimal Touch"}};
inline Values factoryPreset(int i){
 auto v=defaults();
 switch(i){
 case 1:v[Tune]=52;v[Pitch]=42;v[PitchDecay]=26;v[Length]=340;v[Transient]=55;v[Grit]=8;v[Noise]=12;v[Punch]=45;break;
 case 2:v[Tune]=58;v[Pitch]=55;v[PitchDecay]=13;v[Length]=145;v[Sustain]=0;v[Transient]=70;v[ClickLength]=2;v[Grit]=6;v[Sub]=0;break;
 case 3:v[Tune]=32.7;v[Pitch]=22;v[PitchDecay]=65;v[Length]=1100;v[Sustain]=35;v[Sub]=40;v[Transient]=10;v[Noise]=0;v[Grit]=2;break;
 case 4:v[Tune]=47;v[Pitch]=48;v[Length]=680;v[Grit]=23;v[Fold]=38;v[Noise]=30;v[NoiseDecay]=90;v[Transient]=65;v[Sub]=0;v[Output]=-10;break;
 case 5:v[Tune]=43;v[Pitch]=30;v[Length]=390;v[Grit]=17;v[Fold]=15;v[Noise]=45;v[NoiseTone]=2900;v[ClickTone]=1800;v[Transient]=48;v[Punch]=55;break;
 case 6:v[Tune]=41.2;v[Pitch]=16;v[PitchDecay]=80;v[Length]=1600;v[Sustain]=55;v[DecayCurve]=.8;v[Transient]=6;v[Noise]=0;v[Grit]=5;v[Sub]=0;v[Hold]=20;break;
 case 7:v[Tune]=55;v[Pitch]=24;v[PitchDecay]=18;v[Length]=240;v[Transient]=14;v[Noise]=3;v[Grit]=1;v[Sub]=8;v[Sustain]=5;break;
 }return v;
}
class Voice {
 public:
 void start(const Values& values,double rate,float velocity=1){
 p=values;sr=rate;t=0;phase=p[Phase]*pi/180.;subPhase=phase;
 gain=1.-p[Velocity]/100.*(1.-std::clamp(double(velocity),0.,1.));
 rng=0x193a675b;noiseLP=clickLP=dcX=dcY=0;active=true;
 }
 bool playing() const{return active;}
 double age()const{return t;}
 float next(){
 if(!active)return 0;double sum=0;
 for(int os=0;os<2;++os){
 double duration=p[Length]*.001;if(t>=duration){active=false;break;}
 const double attack=p[Attack]*.001,hold=p[Hold]*.001;
 const double u=std::clamp((t-attack-hold)/std::max(.001,duration-attack-hold),0.,1.);
 const double tail=std::pow(std::max(0.,1.-u),p[DecayCurve]),floor=p[Sustain]/100.;
 double env=std::min(1.,t/attack)*((1.-floor)*tail+floor*std::pow(1.-u,.45));
 env*=std::clamp((duration-t)/.008,0.,1.);
 const double sweep=std::exp(-std::pow(t/(p[PitchDecay]*.001),p[PitchCurve]));
 const double freq=std::min(sr*.20,p[Tune]*std::pow(2.,p[Pitch]*sweep/12.));
 phase+=2*pi*freq/(sr*2);if(phase>=2*pi)phase-=2*pi;
 subPhase+=2*pi*p[Tune]*.5/(sr*2);if(subPhase>=2*pi)subPhase-=2*pi;
 rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
 const double white=double(rng)/2147483648.-1.;
 const double na=1.-std::exp(-2*pi*std::min(p[NoiseTone],sr*.4)/(sr*2));
 const double ca=1.-std::exp(-2*pi*std::min(p[ClickTone],sr*.4)/(sr*2));
 noiseLP+=na*(white-noiseLP);clickLP+=ca*(white-clickLP);
 const double click=(white-clickLP)*std::exp(-t/(p[ClickLength]*.001))*p[Transient]/100.*1.4;
 const double noise=noiseLP*std::exp(-t/(p[NoiseDecay]*.001))*p[Noise]/100.*.8;
 const double punch=1.+p[Punch]/100.*1.8*std::exp(-t/.025);
 double body=(std::sin(phase)+std::sin(subPhase)*p[Sub]/100.*.65)*env*punch;
 double x=body+(click+noise)*std::clamp((duration-t)/.008,0.,1.)*std::min(1.,t/.0002);
 const double drive=std::pow(10.,p[Grit]/20.);
 x=std::tanh(x*drive)/std::tanh(drive);
 const double folded=std::sin(x*(1.+p[Fold]/100.*5.5));
 x=x*(1.-p[Fold]/100.)+folded*p[Fold]/100.;
 double dc=x-dcX+std::exp(-2*pi*8./(sr*2))*dcY;dcX=x;dcY=dc;
 sum+=dc*std::clamp((duration-t)/.005,0.,1.);
 t+=1./(sr*2);
 }
 return float(std::clamp(sum*.5*gain*std::pow(10.,p[Output]/20.),-.999,.999));
 }
 private:
 Values p=defaults();double sr=48000,t=0,phase=0,subPhase=0,gain=1,noiseLP=0,clickLP=0,dcX=0,dcY=0;
 uint32_t rng=0;bool active=false;
};
class Engine{
 public:
 void prepare(double rate){sr=rate;reset();}
 void reset(){voices={};}
 void trigger(const Values& p,float velocity=1){
 Voice* target=&voices[0];
 for(auto& v:voices){if(!v.playing()){target=&v;break;}if(v.age()>target->age())target=&v;}
 target->start(p,sr,velocity);
 }
 float next(){double x=0;for(auto& v:voices)x+=v.next();return float(std::clamp(x,-.999,.999));}
 private:double sr=48000;std::array<Voice,16> voices{};
};
inline std::vector<float> render(const Values&p,double sr=48000){
 std::vector<float> out(size_t(std::ceil(p[Length]*sr/1000.))+64);
 Voice v;v.start(p,sr);for(auto&x:out)x=v.next();return out;
}
}