#include "KickEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
using namespace phasekick;
namespace {
const char* ids[]={"tune","pitch","pitchDecay","pitchCurve","length","attack","hold","sustain","decayCurve","transient","clickTone","clickLength","noise","noiseDecay","noiseTone","grit","fold","sub","punch","phase","velocity","output"};
class Synth:public juce::AudioProcessor {
public:
 juce::AudioProcessorValueTreeState state;
 std::array<std::atomic<float>*,Preset> raw{};
 Engine engine;
 std::atomic<bool> audition{false};
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
 void prepareToPlay(double rate,int)override{engine.prepare(rate);}
 void releaseResources()override{engine.reset();}
 bool isBusesLayoutSupported(const BusesLayout& b)const override{return b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&b.getMainInputChannelSet().isDisabled();}
 void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi)override{
 juce::ScopedNoDenormals guard;b.clear();int cursor=0;
 if(audition.exchange(false))engine.trigger(values());
 auto fill=[&](int until){for(;cursor<until;++cursor){auto x=engine.next();for(int ch=0;ch<b.getNumChannels();++ch)b.setSample(ch,cursor,x);}};
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
 std::array<juce::Slider,Preset> knobs;
 std::array<juce::Label,Preset> labels;
 std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
 std::unique_ptr<juce::FileChooser> chooser;
 void timerCallback()override{repaint(20,104,1010,184);}
public:
 explicit Editor(Synth& synth):AudioProcessorEditor(synth),s(synth){
 setSize(1080,760);
 for(int i=0;i<8;++i)presets.addItem(presetNames[i],i+1);
 presets.setSelectedId(1,juce::dontSendNotification);
 presets.onChange=[this]{s.loadPreset(presets.getSelectedId()-1);};
 addAndMakeVisible(presets);
 audition.onClick=[this]{s.audition.store(true);};
 addAndMakeVisible(audition);
 save.onClick=[this]{exportWav();};
 addAndMakeVisible(save);
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
 g.setColour(juce::Colour(0xffc4f16b));g.setFont(30.f);g.drawText("phasekick",25,15,350,48,juce::Justification::left);
 g.setFont(11.f);g.drawText("KICK SYNTHESIZER",28,63,300,22,juce::Justification::left);
 g.setColour(juce::Colour(0xff1b2318));g.fillRoundedRectangle(24.f,109.f,1032.f,199.f,6.f);
 auto w=render(s.values(),12000);
 if(!w.empty()){
 juce::Path line;
 for(int x=0;x<998;++x){
 size_t i=std::min(w.size()-1,size_t(x)*w.size()/998);
 float y=206.f-w[i]*75.f;if(!x)line.startNewSubPath(42.f,y);else line.lineTo(float(x+42),y);
 }
 g.setColour(juce::Colour(0xffc4f16b));g.strokePath(line,juce::PathStrokeType(1.3f));
 }
 g.setFont(12.f);g.drawText("Any MIDI note triggers a kick. Export WAV for a one-shot sample.",28,728,1010,24,juce::Justification::left);
 }
};
juce::AudioProcessorEditor* Synth::createEditor(){return new Editor(*this);}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new Synth();}
