#include "KickEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <random>
using namespace phasekick;
namespace {
const char* ids[]={"tune","pitch","pitchDecay","pitchCurve","length","attack","hold","sustain","decayCurve","transient","clickTone","clickLength","noise","noiseDecay","noiseTone","grit","fold","sub","punch","phase","velocity","output"};
class Synth:public juce::AudioProcessor {
public:
 juce::AudioProcessorValueTreeState state;
 std::array<std::atomic<float>*,Preset> raw{};
 Engine engine;
 std::atomic<bool> audition{false};
 std::atomic<bool> looping{false};
 std::atomic<float> bpm{128.f};
 double beatCounter=0.0;
 Synth():AudioProcessor(BusesProperties().withOutput("Stereo",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"Phasekick",layout()){
 for(int i=0;i<Preset;++i)raw[i]=state.getRawParameterValue(ids[i]);}
 static juce::AudioProcessorValueTreeState::ParameterLayout layout(){
 juce::AudioProcessorValueTreeState::ParameterLayout out;
 for(int i=0;i<Preset;++i){
 const auto s=specs[i];float skew=float(s.skew);
 juce::NormalisableRange<float> range(float(s.min),float(s.max),
 [skew](float a,float b,float n){return a+(b-a)*std::pow(n,skew);},
 [skew](float a,float b,float n){return std::pow(juce::jlimit(0.f,1.f,(n-a)/(b-a)),1.f/skew);});
 out.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[i],1},s.name,range,float(s.initial)));
 }return out;
 }
 Values values()const{Values v{};for(int i=0;i<Preset;++i)v[i]=raw[i]->load();return v;}
 void loadPreset(int n){auto v=factoryPreset(n);for(int i=0;i<Preset;++i){auto*p=state.getParameter(ids[i]);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(v[i])));p->endChangeGesture();}}
 void prepareToPlay(double rate,int)override{engine.prepare(rate);beatCounter=0.0;}
 void releaseResources()override{engine.reset();}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainInputChannelSet().isDisabled();}
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)override{
 juce::ScopedNoDenormals guard;b.clear();int cursor=0;
 if(audition.exchange(false))engine.trigger(values());
 auto fill=[&](int until){for(;cursor<until;++cursor){
 if(looping.load(std::memory_order_relaxed)){
  if(beatCounter<=0.0){engine.trigger(values());beatCounter+=getSampleRate()*60.0/juce::jlimit(40.f,240.f,bpm.load(std::memory_order_relaxed));}
  beatCounter-=1.0;
 } else beatCounter=0.0;
 auto x=engine.next();for(int ch=0;ch<b.getNumChannels();++ch)b.setSample(ch,cursor,x);
 }};
 for(const auto m:midi){auto msg=m.getMessage();fill(juce::jlimit(cursor,b.getNumSamples(),m.samplePosition));
 if(msg.isNoteOn())engine.trigger(values(),msg.getFloatVelocity());
 else if(msg.isAllNotesOff()||msg.isAllSoundOff())engine.reset();}
 fill(b.getNumSamples());midi.clear();
 }
 juce::AudioProcessorEditor* createEditor()override;
 bool hasEditor()const override{return true;}
 const juce::String getName()const override{return "Phasekick";}
 bool acceptsMidi()const override{return true;}
 bool producesMidi()const override{return false;}
 bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 2.1;}
 int getNumPrograms()override{return 1;}
 int getCurrentProgram()override{return 0;}
 void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return "Current sound";}
 void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock& b)override{if(auto xml=state.copyState().createXml())copyXmlToBinary(*xml,b);}
 void setStateInformation(const void*d,int n)override{if(auto xml=getXmlFromBinary(d,n))if(xml->hasTagName(state.state.getType()))state.replaceState(juce::ValueTree::fromXml(*xml));}
};
class Editor:public juce::AudioProcessorEditor,private juce::Timer{
 Synth&s;
 juce::ComboBox presets;
 juce::TextButton audition{"Audition"};
 juce::TextButton save{"Export WAV"};
 juce::TextButton loop{"Loop: OFF"},mutate{"Mutate"},reset{"Reset"},sync{"1/16 length"};
 juce::Slider tempo;
 juce::Label tempoLabel;
 juce::Label title,subtitle,scopeLabel,libraryLabel,groupLabel;
 std::array<juce::TextButton,8> presetButtons;
 std::mt19937 rng{std::random_device{}()};
 int activePreset=0;
 int view=0;
 juce::TextButton waveform{"Waveform"},pitchView{"Pitch envelope"},ampView{"Amp envelope"};

 std::array<juce::Slider,Preset> knobs;
 std::array<juce::Label,Preset> labels;
 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
 std::unique_ptr<juce::FileChooser> chooser;
 void timerCallback()override{repaint(20,104,1010,184);}
public:
 explicit Editor(Synth& synth):AudioProcessorEditor(synth),s(synth){
 setSize(1240,820);
 for(int i=0;i<8;++i)presets.addItem(presetNames[i],i+1);
 presets.setSelectedId(1,juce::dontSendNotification);
 presets.onChange=[this]{activePreset=presets.getSelectedId()-1;s.loadPreset(activePreset);updatePresets();};
 addAndMakeVisible(presets);
 audition.onClick=[this]{s.audition.store(true);};
 addAndMakeVisible(audition);
 save.onClick=[this]{exportWav();};
 addAndMakeVisible(save);
 loop.onClick=[this]{bool enabled=!s.looping.load();s.looping.store(enabled);loop.setButtonText(enabled?"Loop: ON":"Loop: OFF");loop.setColour(juce::TextButton::buttonColourId,enabled?juce::Colour(0xff557e29):juce::Colour(0xff1b2318));};addAndMakeVisible(loop);
 mutate.onClick=[this]{std::uniform_real_distribution<float> variation(-.11f,.11f);for(int i=0;i<Preset;++i){if(i==Tune||i==Length||i==Output||i==Velocity||i==Phase)continue;auto* param=s.state.getParameter(ids[i]);float n=param->getValue();param->beginChangeGesture();param->setValueNotifyingHost(juce::jlimit(0.f,1.f,n+variation(rng)));param->endChangeGesture();}repaint();};addAndMakeVisible(mutate);
 reset.onClick=[this]{s.loadPreset(activePreset);};addAndMakeVisible(reset);
 sync.onClick=[this]{auto* param=s.state.getParameter(ids[Length]);float val=15000.f/s.bpm.load();param->beginChangeGesture();param->setValueNotifyingHost(param->convertTo0to1(juce::jlimit(60.f,2000.f,val)));param->endChangeGesture();};addAndMakeVisible(sync);
 tempo.setRange(40,240,1);tempo.setValue(128);tempo.setSliderStyle(juce::Slider::LinearBar);tempo.setTextBoxStyle(juce::Slider::TextBoxLeft,false,54,24);tempo.onValueChange=[this]{s.bpm.store(float(tempo.getValue()));};addAndMakeVisible(tempo);
 tempoLabel.setText("BPM",juce::dontSendNotification);tempoLabel.setColour(juce::Label::textColourId,juce::Colour(0xffa1b48d));addAndMakeVisible(tempoLabel);
 title.setText("phasekick   01",juce::dontSendNotification);title.setFont(juce::Font(juce::FontOptions(28.f,juce::Font::bold)));title.setColour(juce::Label::textColourId,juce::Colour(0xffc4f16b));addAndMakeVisible(title);
 subtitle.setText("KICK SYNTHESIZER   /   DESIGNED TO HIT DIFFERENT.",juce::dontSendNotification);subtitle.setColour(juce::Label::textColourId,juce::Colour(0xff8a9d77));addAndMakeVisible(subtitle);
 libraryLabel.setText("SOUND LIBRARY    08",juce::dontSendNotification);libraryLabel.setColour(juce::Label::textColourId,juce::Colour(0xffa5b78f));addAndMakeVisible(libraryLabel);
 scopeLabel.setText("OUTPUT SIGNAL",juce::dontSendNotification);scopeLabel.setColour(juce::Label::textColourId,juce::Colour(0xff9bb985));addAndMakeVisible(scopeLabel);
 for(int i=0;i<8;++i){presetButtons[i].setButtonText(presetNames[i]);presetButtons[i].onClick=[this,i]{activePreset=i;s.loadPreset(i);presets.setSelectedId(i+1,juce::dontSendNotification);updatePresets();};addAndMakeVisible(presetButtons[i]);}
 waveform.onClick=[this]{view=0;scopeLabel.setText("OUTPUT SIGNAL",juce::dontSendNotification);repaint();};
 pitchView.onClick=[this]{view=1;scopeLabel.setText("SEMITONES ABOVE ROOT",juce::dontSendNotification);repaint();};
 ampView.onClick=[this]{view=2;scopeLabel.setText("BODY AMPLITUDE",juce::dontSendNotification);repaint();};
 addAndMakeVisible(waveform);addAndMakeVisible(pitchView);addAndMakeVisible(ampView);
 updatePresets();
 for(int i=0;i<Preset;++i){
 labels[i].setText(specs[i].name,juce::dontSendNotification);
 labels[i].setJustificationType(juce::Justification::centred);
 labels[i].setColour(juce::Label::textColourId,juce::Colours::lightgreen);
 addAndMakeVisible(labels[i]);
 knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
 knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,20);
 knobs[i].setTextValueSuffix(juce::String(" ")+specs[i].unit);
 addAndMakeVisible(knobs[i]);
 attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s.state,ids[i],knobs[i]));
 }
 startTimerHz(12);
 }
 ~Editor()override{stopTimer();}
 void updatePresets(){for(int i=0;i<8;++i){presetButtons[i].setColour(juce::TextButton::buttonColourId,i==activePreset?juce::Colour(0xff344c23):juce::Colour(0xff181f16));presetButtons[i].setColour(juce::TextButton::textColourOffId,i==activePreset?juce::Colour(0xffd9f4b3):juce::Colour(0xffa6b49b));}}
 void exportWav(){
 chooser=std::make_unique<juce::FileChooser>("Export Phasekick WAV",juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Phasekick.wav"),"*.wav");
 chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<Editor>(this)](const juce::FileChooser&fc){
 if(!safe)return;
 auto file=fc.getResult();if(file==juce::File())return;
 auto samples=render(safe->s.values());auto stream=file.createOutputStream();
 if(!stream)return;
 juce::WavAudioFormat fmt;
 std::unique_ptr<juce::AudioFormatWriter> writer(fmt.createWriterFor(stream.get(),48000,1,24,{},0));
 if(writer){stream.release();const float*channels[]{samples.data()};writer->writeFromFloatArrays(channels,1,int(samples.size()));}
 });
 }
 void resized()override{
 title.setBounds(26,13,330,46);subtitle.setBounds(348,20,650,36);
 libraryLabel.setBounds(24,83,197,32);
 for(int i=0;i<8;++i)presetButtons[i].setBounds(22,128+i*62,198,51);
 presets.setBounds(260,91,237,36);mutate.setBounds(524,91,115,36);reset.setBounds(647,91,90,36);
 waveform.setBounds(262,155,117,32);pitchView.setBounds(385,155,149,32);ampView.setBounds(540,155,137,32);scopeLabel.setBounds(1010,156,197,30);
 for(int i=0;i<Preset;++i){
 int x=257+(i%6)*158,y=431+(i/6)*95;
 labels[i].setBounds(x,y,150,18);knobs[i].setBounds(x,y+18,150,72);
 }
 audition.setBounds(266,759,135,42);loop.setBounds(415,759,108,42);tempo.setBounds(536,760,89,34);
 tempoLabel.setBounds(625,760,45,34);sync.setBounds(684,759,110,42);
 save.setBounds(998,759,205,42);
 }
 void exportWav(){
 chooser=std::make_unique<juce::FileChooser>("Export Phasekick WAV",juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Phasekick.wav"),"*.wav");
 chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<Editor>(this)](const juce::FileChooser&fc){
 if(!safe)return;
 auto file=fc.getResult();if(file==juce::File())return;
 auto samples=render(safe->s.values());auto stream=file.createOutputStream();
 if(!stream)return;
 juce::WavAudioFormat fmt;
 std::unique_ptr<juce::AudioFormatWriter> writer(fmt.createWriterFor(stream.get(),48000,1,24,{},0));
 if(writer){stream.release();const float*channels[]{samples.data()};writer->writeFromFloatArrays(channels,1,int(samples.size()));}
 });
 }
 void resized()override{
 presets.setBounds(400,20,260,40);audition.setBounds(680,20,160,40);save.setBounds(860,20,180,40);
 for(int i=0;i<Preset;++i){
 int x=22+(i%6)*175,y=345+(i/6)*100;
 labels[i].setBounds(x,y,160,18);knobs[i].setBounds(x,y+18,160,82);
 }
 }
 void paint(juce::Graphics&g)override{
 g.fillAll(juce::Colour(0xff121612));
 g.setColour(juce::Colour(0xffc4f16b));g.fillRect(0,0,getWidth(),3);
 g.setColour(juce::Colour(0xff242e20));g.fillRect(0,72,getWidth(),1);
 g.setColour(juce::Colour(0xff171c16));g.fillRect(0,75,238,668);
 g.setColour(juce::Colour(0xff263324));g.fillRect(238,75,1,668);
 g.setColour(juce::Colour(0xff1b2318));g.fillRoundedRectangle(255.f,148.f,961.f,244.f,5.f);
 g.setColour(juce::Colour(0xff243023));for(int i=0;i<11;++i){float x=275.f+i*91.f;g.drawLine(x,198.f,x,373.f,.8f);}
 for(int i=0;i<5;++i){float y=198.f+i*43.75f;g.drawLine(275.f,y,1196.f,y,.8f);}
 auto v=s.values();float left=276.f,top=202.f,w=920.f,h=165.f,mid=top+h*.5f;
 auto waveformSamples=render(v,12000);
 if(!waveformSamples.empty()){
 juce::Path line;
 for(int x=0;x<920;++x){
 float pos=float(x)/920.f;
 float y=mid;
 if(view==0){size_t i=std::min(waveformSamples.size()-1,size_t(pos*waveformSamples.size()));y=mid-waveformSamples[i]*75.f;}
 else if(view==1){double time=pos*std::max(150.,v[PitchDecay]*3.);double st=v[Pitch]*std::exp(-std::pow(time/v[PitchDecay],v[PitchCurve]));y=top+h-float(st/72.*h);}
 else {double dur=v[Length],t=pos*dur,a=v[Attack],hold=v[Hold],u=juce::jlimit(0.,1.,(t-a-hold)/std::max(1.,dur-a-hold));double tail=std::pow(std::max(0.,1.-u),v[DecayCurve]);double amp=std::min(1.,t/a)*((1.-v[Sustain]/100.)*tail+v[Sustain]/100.*std::pow(1.-u,.45));amp*=juce::jlimit(0.,1.,(dur-t)/8.);y=top+h-float(amp*h);}
 if(x==0)line.startNewSubPath(left,y);else line.lineTo(left+x,y);
 }
 g.setColour(juce::Colour(0xffc4f16b));g.strokePath(line,juce::PathStrokeType(1.4f));
 }
 g.setFont(11.f);g.setColour(juce::Colour(0xff8ca579));
 g.drawText(juce::String(v[Tune],1)+" Hz   /   "+juce::String(v[Length],0)+" ms   /   48 kHz",265,396,940,22,juce::Justification::left);
 const char* groups[]={"01  PITCH","02  AMPLITUDE","03  TRANSIENT + NOISE","04  CHARACTER"};
 for(int i=0;i<4;++i){g.setColour(juce::Colour(0xff1b2318));g.fillRoundedRectangle(255.f+i*240.f,460.f,235.f,260.f,4.f);g.setColour(juce::Colour(0xffb7da8c));g.setFont(11.f);g.drawText(groups[i],264+i*240,460,225,22,juce::Justification::left);}
 g.setColour(juce::Colour(0xff293626));g.fillRect(0,741,getWidth(),1);
 g.setColour(juce::Colour(0xff192218));g.fillRect(0,742,getWidth(),78);
 }

};
juce::AudioProcessorEditor* Synth::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Synth();}
