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
class Editor : public juce::AudioProcessorEditor, private juce::Timer {
 Synth& s;
 juce::ComboBox presets, zoom;
 juce::TextButton audition{"Audition"}, loop{"Loop off"}, mutate{"Mutate"}, reset{"Reset"}, sync{"1/16 length"}, save{"Export WAV"};
 juce::TextButton waveform{"Waveform"}, pitchView{"Pitch"}, ampView{"Amplitude"};
 juce::Slider tempo;
 juce::Label tempoLabel, header, subtitle, libraryLabel, scopeLabel, dragHint, status;
 std::array<juce::TextButton,8> presetButtons;
 std::array<juce::Slider,Preset> knobs;
 std::array<juce::Label,Preset> labels;
 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
 std::unique_ptr<juce::FileChooser> chooser;
 std::mt19937 rng{std::random_device{}()};
 int activePreset=0, view=0;
 float scale=1.0f;
 bool dragStarted=false;
 static constexpr int baseW=1260, baseH=936;
 const juce::Colour bg{0xff101511}, panel{0xff1b241c}, foreground{0xffe4ebdf}, muted{0xff9fac99}, accent{0xffc0ed79};
 void timerCallback() override {repaint(region(264,215,958,219));}
 juce::Rectangle<int> region(int x,int y,int w,int h)const{
  return {juce::roundToInt(x*scale),juce::roundToInt(y*scale),juce::roundToInt(w*scale),juce::roundToInt(h*scale)};
 }
 void set(juce::Component& c,int x,int y,int w,int h){c.setBounds(region(x,y,w,h));}
 bool writeWav(const juce::File& f){
  auto samples=render(s.values());
  auto stream=f.createOutputStream();
  if(!stream)return false;
  juce::WavAudioFormat format;
  std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(),48000.0,1,24,{},0));
  if(!writer)return false;
  stream.release();
  const float* channels[]{samples.data()};
  return writer->writeFromFloatArrays(channels,1,(int)samples.size());
 }
 void exportWav(){
  chooser=std::make_unique<juce::FileChooser>("Export Phasekick WAV",juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Phasekick.wav"),"*.wav");
  chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,
   [safe=juce::Component::SafePointer<Editor>(this)](const juce::FileChooser& fc){
    if(!safe)return;
    auto file=fc.getResult();
    if(file!=juce::File())safe->status.setText(safe->writeWav(file)?"WAV exported":"Export failed",juce::dontSendNotification);
   });
 }
 void updatePresets(){
  for(int i=0;i<8;++i){
   presetButtons[i].setColour(juce::TextButton::buttonColourId,i==activePreset?juce::Colour(0xff344b2b):panel);
   presetButtons[i].setColour(juce::TextButton::textColourOffId,i==activePreset?accent:foreground);
  }
 }
 void startExternalDrag(){
  auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Phasekick-drag");
  if(!dir.isDirectory()&&!dir.createDirectory())return;
  auto file=dir.getNonexistentChildFile("Phasekick", ".wav", false);
  if(!writeWav(file)){status.setText("Could not create WAV",juce::dontSendNotification);return;}
  status.setText("Drop WAV onto an Ableton audio track",juce::dontSendNotification);
  // Windows host integration depends on its external file-drop handling.
  juce::DragAndDropContainer::performExternalDragDropOfFiles(juce::StringArray{file.getFullPathName()},false,this);
 }
 public:
 explicit Editor(Synth& synth):juce::AudioProcessorEditor(synth),s(synth){
  setSize(baseW,baseH);
  setResizable(true,false);
  setResizeLimits(1008,749,1512,1124);
  header.setText("phasekick",juce::dontSendNotification);
  header.setFont(juce::Font(juce::FontOptions(30.f,juce::Font::bold)));
  header.setColour(juce::Label::textColourId,accent);
  subtitle.setText("KICK SYNTHESIZER  /  SOUND DESIGN",juce::dontSendNotification);
  libraryLabel.setText("PRESETS",juce::dontSendNotification);
  scopeLabel.setText("WAVEFORM",juce::dontSendNotification);
  dragHint.setText("DRAG WAVEFORM TO ABLETON  ↗",juce::dontSendNotification);
  status.setText("Drag from the waveform to export a WAV",juce::dontSendNotification);
  for(auto* l:{&subtitle,&libraryLabel,&scopeLabel,&dragHint,&status,&tempoLabel}){
   l->setColour(juce::Label::textColourId,muted);
  }
  dragHint.setColour(juce::Label::textColourId,accent);
  for(auto* l:{&header,&subtitle,&libraryLabel,&scopeLabel,&dragHint,&status,&tempoLabel})addAndMakeVisible(*l);
  for(int i=0;i<8;++i){
   presets.addItem(presetNames[i],i+1);
   presetButtons[i].setButtonText(presetNames[i]);
   presetButtons[i].onClick=[this,i]{activePreset=i; s.loadPreset(i);presets.setSelectedId(i+1,juce::dontSendNotification);updatePresets();};
   addAndMakeVisible(presetButtons[i]);
  }
  presets.setSelectedId(1,juce::dontSendNotification);
  presets.onChange=[this]{activePreset=juce::jmax(0,presets.getSelectedId()-1);s.loadPreset(activePreset);updatePresets();};
  addAndMakeVisible(presets);
  updatePresets();
  audition.onClick=[this]{s.audition.store(true);};
  loop.onClick=[this]{auto on=!s.looping.load();s.looping.store(on);loop.setButtonText(on?"Loop on":"Loop off");};
  mutate.onClick=[this]{
   std::uniform_real_distribution<float> delta(-.11f,.11f);
   for(int i=0;i<Preset;++i){if(i==Tune||i==Length||i==Output||i==Velocity||i==Phase)continue;
    auto* p=s.state.getParameter(ids[i]);p->beginChangeGesture();p->setValueNotifyingHost(juce::jlimit(0.f,1.f,p->getValue()+delta(rng)));p->endChangeGesture();}
   repaint();
  };
  reset.onClick=[this]{s.loadPreset(activePreset);};
  sync.onClick=[this]{
   auto* p=s.state.getParameter(ids[Length]);auto val=15000.f/s.bpm.load();
   p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(60.f,2000.f,val)));p->endChangeGesture();
  };
  save.onClick=[this]{exportWav();};
  waveform.onClick=[this]{view=0;scopeLabel.setText("WAVEFORM",juce::dontSendNotification);repaint();};
  pitchView.onClick=[this]{view=1;scopeLabel.setText("PITCH ENVELOPE",juce::dontSendNotification);repaint();};
  ampView.onClick=[this]{view=2;scopeLabel.setText("AMPLITUDE ENVELOPE",juce::dontSendNotification);repaint();};
  for(auto* t:{&audition,&loop,&mutate,&reset,&sync,&save,&waveform,&pitchView,&ampView}){
   t->setColour(juce::TextButton::buttonColourId,panel);
   t->setColour(juce::TextButton::textColourOffId,foreground);
   addAndMakeVisible(*t);
  }
  tempo.setRange(40,240,1);tempo.setValue(128);
  tempo.setSliderStyle(juce::Slider::LinearBar);tempo.setTextBoxStyle(juce::Slider::TextBoxLeft,false,60,26);
  tempo.onValueChange=[this]{s.bpm.store((float)tempo.getValue());};
  addAndMakeVisible(tempo);
  tempoLabel.setText("BPM",juce::dontSendNotification);
  zoom.addItem("80%",1);zoom.addItem("90%",2);zoom.addItem("100%",3);zoom.addItem("110%",4);zoom.addItem("120%",5);
  zoom.setSelectedId(3,juce::dontSendNotification);
  zoom.onChange=[this]{const float levels[]{.8f,.9f,1.f,1.1f,1.2f};int i=juce::jlimit(0,4,zoom.getSelectedId()-1);scale=levels[i];setSize(juce::roundToInt(baseW*scale),juce::roundToInt(baseH*scale));};
  addAndMakeVisible(zoom);
  for(int i=0;i<Preset;++i){
   labels[i].setText(specs[i].name,juce::dontSendNotification);
   labels[i].setJustificationType(juce::Justification::centred);
   labels[i].setColour(juce::Label::textColourId,muted);
   addAndMakeVisible(labels[i]);
   knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
   knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow,false,92,22);
   // Match each parameter's natural precision, avoiding JUCE's long float values.
   knobs[i].setNumDecimalPlacesToDisplay(i==Tune||i==PitchCurve||i==DecayCurve||i==Attack?1:0);
   knobs[i].setTextValueSuffix(juce::String(" ")+specs[i].unit);
   knobs[i].setColour(juce::Slider::rotarySliderFillColourId,accent);
   knobs[i].setColour(juce::Slider::textBoxTextColourId,foreground);
   knobs[i].setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
   addAndMakeVisible(knobs[i]);
   attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(s.state,ids[i],knobs[i]));
  }
  startTimerHz(8);
 }
 ~Editor()override{stopTimer();}
 void mouseDown(const juce::MouseEvent& e)override{dragStarted=false;juce::ignoreUnused(e);}
 void mouseDrag(const juce::MouseEvent& e)override{
  if(!dragStarted&&view==0&&region(261,206,973,219).contains(e.getMouseDownPosition())){
   dragStarted=true;startExternalDrag();
  }
 }
 void mouseUp(const juce::MouseEvent&)override{dragStarted=false;}
 void resized()override{
  scale=(float)getWidth()/baseW;
  set(header,28,17,250,43);set(subtitle,310,19,600,35);set(zoom,1092,22,122,32);
  set(libraryLabel,23,90,180,29);
  for(int i=0;i<8;++i)set(presetButtons[i],22,128+i*62,196,48);
  set(presets,263,91,270,40);set(mutate,548,91,125,40);set(reset,687,91,100,40);
  set(waveform,267,162,124,34);set(pitchView,401,162,130,34);set(ampView,541,162,130,34);
  set(scopeLabel,930,162,270,34);set(dragHint,267,432,360,27);
  for(int i=0;i<Preset;++i){const int x=263+(i%6)*161,y=485+(i/6)*88;set(labels[i],x,y,148,20);set(knobs[i],x,y+19,148,67);}
  set(audition,263,869,137,42);set(loop,412,869,108,42);
  set(tempo,531,873,108,33);set(tempoLabel,638,872,44,35);
  set(sync,699,869,128,42);set(save,1012,869,198,42);
  set(status,265,826,750,26);
 }
 void paint(juce::Graphics& g)override{
  g.addTransform(juce::AffineTransform::scale(scale));
  g.fillAll(bg);
  g.setColour(accent);g.fillRect(0,0,baseW,3);
  g.setColour(juce::Colour(0xff263226));g.fillRect(0,76,baseW,1);
  g.setColour(juce::Colour(0xff151d17));g.fillRect(0,78,241,742);
  g.setColour(juce::Colour(0xff29362a));g.fillRect(241,78,1,742);
  g.setColour(panel);g.fillRoundedRectangle(259.f,153.f,977.f,271.f,9.f);
  g.setColour(juce::Colour(0xff334233));
  for(int i=0;i<11;++i){float x=276.f+i*94.f;g.drawLine(x,214.f,x,408.f,.6f);}
  for(int i=0;i<5;++i){float y=215.f+i*48.f;g.drawLine(273.f,y,1218.f,y,.6f);}
  auto v=s.values();auto samples=render(v,12000);
  if(!samples.empty()){
   juce::Path path;
   for(int i=0;i<922;++i){
    double t=(double)i/921., y=317.;
    if(view==0){size_t ix=std::min(samples.size()-1,(size_t)(t*samples.size()));y=317.-samples[ix]*85.;}
    else if(view==1){double ms=t*std::max(150.,v[PitchDecay]*3.);double st=v[Pitch]*std::exp(-std::pow(ms/v[PitchDecay],v[PitchCurve]));y=406.-st/72.*190.;}
    else {double ms=t*v[Length],a=v[Attack],hold=v[Hold],u=juce::jlimit(0.,1.,(ms-a-hold)/std::max(1.,v[Length]-a-hold));double tail=std::pow(std::max(0.,1.-u),v[DecayCurve]);double amp=std::min(1.,ms/a)*((1.-v[Sustain]/100.)*tail+v[Sustain]/100.*std::pow(1.-u,.45));y=406.-amp*188.;}
    if(i==0)path.startNewSubPath(276.f,(float)y);else path.lineTo(276.f+i,(float)y);
   }
   g.setColour(accent);g.strokePath(path,juce::PathStrokeType(1.5f));
  }
  g.setColour(muted);g.setFont(juce::FontOptions(12.f));
  g.drawText(juce::String(v[Tune],1)+" Hz   ·   "+juce::String(v[Length],0)+" ms",875,430,340,22,juce::Justification::right);
  g.setColour(juce::Colour(0xff29372a));g.fillRect(245,461,992,1);
  g.setColour(juce::Colour(0xff2b392a));g.fillRect(0,855,baseW,1);
  g.setColour(juce::Colour(0xff151d17));g.fillRect(0,856,baseW,80);
 }
};
juce::AudioProcessorEditor* Synth::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Synth();}
