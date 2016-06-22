#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <math.h>

#include "RtMidi.h"
#include "bcr2000Driver.h"
#include "synthesiser.h"

#include "Stk.h"
#include "RtAudio.h"
#include "RtError.h"

#include <boost/thread/mutex.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>

ofstream mainlog ("main.log");



BCR2000Driver bcr2000Driver;

using namespace stk;

int rtaudio_callback(
		     void			*outbuf,
		     void			*inbuf,
		     unsigned int		nFrames,
		     double			streamtime,
		     RtAudioStreamStatus	status,
		     void * data
		     );

void midiCallback( double deltatime, std::vector< unsigned char > *message, void *classptr );


class MainApplication: public boost::mutex
{
public:

  map<int,RPolySynthesiser> synths;
  RPolySynthesiser focalSynth;
  
  shared_ptr<RtMidiIn> midiIn;
  shared_ptr<RtMidiIn> bcrIn;
  shared_ptr<RtMidiOut> midiOut;

  double sampleRate;
  unsigned int numChannels;

  shared_ptr<RtAudio> audioOut;
  unsigned int bufsize = 512;

  double timePoint;

  bool finished;
  bool pause;

  MainApplication () 
    : sampleRate (48000), numChannels (2), 
      timePoint (0.0),
      finished(false),pause(false) {
    synths[0] = RPolySynthesiser(new PolySynthesiser(16,6));
    focalSynth = synths[0];
  }
  
  void init() {
    cout << "Initialising sound output" << endl;
    if (setupAudio()) 
      throw "Error";
    cout << "Initialising Midi input" << endl;
    if (setupMidi())
      throw "Error";
  }
  void run() {
    
    if (!audioOut) 
      throw "No audio output";

    audioOut->startStream();
    synths[0]->load ("default.prm");
    while (!finished) {
      #if 1
      cout << ">> ";
      cout.flush();
      string input;
      getline(cin,input);
      if (input[0] == 'p')
	pause = pause? false:true;

      if (input[0] == 'n') {
	// create a new synth
	int channel=-1, numPhones=-1, numPartials = -1;
	stringstream sstr(input) ;
	string junk;
	sstr >> junk >> channel >> numPartials >> numPhones;
	cout << "New synth on channel: " << channel << endl
	     << "Partials = " << numPartials << endl
	     << "Phones = " << numPhones << endl;
	if (channel > 0 && numPartials>0 && numPhones>0) {
	  channel -=1;
	  synths[channel] = RPolySynthesiser(new PolySynthesiser (numPartials, numPhones));
	  focalSynth = synths[channel];
	}
	else
	  cerr << "Usage:" << endl << "n <midi channel> <num partials> <num phones>" << endl;
      }

      if (input[0] == 'q')
	finished = true;

      if (input[0] == 'o') {
	focalSynth->controlSet->outputControllersToConsole();
      }

      if (input[0] == 's') {
	if (input.size() > 3) {
	  focalSynth->save (input.substr(2));
	}
	else 
	  cout << "Enter a filename!" << endl;
      }
      if (input[0] == 'l') {
	if (input.size() > 3) {
	  focalSynth->load (input.substr(2));
	}
	else 
	  cout << "Enter a filename!" << endl;
      }
      if (input[0] == 'm') {
	string junk,fn1,fn2;
	stringstream (input) >> junk >> fn1 >> fn2;
	if (fn1.size() > 0 && fn2.size() > 0) {
	  focalSynth->loadMorph (fn1,fn2);
	}
	else 
	  cout << "Enter filenames!" << endl;
      }
      if (input[0] >='0' && input[0]<='9') {
	int numChannel = -1;
	stringstream(input) >> numChannel;
	numChannel-= 1;
	if (synths[numChannel]) {
	  focalSynth = synths[numChannel];
	  focalSynth->updateController();
	}
      }

#else
      sleep(1);
#endif

    }
    audioOut->stopStream();
    audioOut->closeStream();  
  }

  bool chooseMidiPort( )
  {
    //  std::cout << "\nWould you like to open a virtual input port? [y/N] ";
    
    //  std::string keyHit;
    //  std::getline( std::cin, keyHit );
    //  if ( keyHit == "y" ) {
    //    rtmidi->openVirtualPort();
    //    return true;
    //  }
    
    std::string portName;
    unsigned int nPorts = midiIn->getPortCount();
    if ( nPorts == 0 ) {
      std::cout << "No input ports available!" << std::endl;
      return false;
    }
    
    int bcrport = -1;
    int vmpport = -1;
    
    for (unsigned int i=0; i<nPorts; i++ ) {
      portName = midiIn->getPortName(i);
      if (portName.substr(0,7)=="BCR2000" && bcrport == -1)
	bcrport = i;
      if (portName.substr(0,11)=="VMPK Output")
	vmpport = i;
      std::cout << "  Input port #" << i << ": " << portName << '\n';
    }

    cout << "Choose midi in";
    if (vmpport >-1) 
      cout << " (default " << vmpport << ")";
    cout << endl << ": ";
    string input;
    getline(cin,input);

    int inputPort = vmpport;
    if (input.size()>0)
      stringstream(input) >> inputPort;
    
    if (inputPort >-1)
      midiIn->openPort( inputPort );
    else
      cerr << "No input port!" << endl;
    
    if (bcrport == -1) {
      std::cout << "BCR2000 not found, just not going to talk to it then. \nSee if I care." << endl;
    }
    else {
      midiOut->openPort( bcrport );
      bcrIn->openPort(bcrport);
    }
    
    return true;
  }
  
  
  int setupMidi () {
    midiIn = shared_ptr<RtMidiIn> (new RtMidiIn());
    midiOut = shared_ptr<RtMidiOut> (new RtMidiOut());
    bcrIn = shared_ptr<RtMidiIn> (new RtMidiIn());
    // Call function to select port.
    if (!chooseMidiPort()) {
      cerr << "Failed to set up midi" <<endl;
      return 1;
  }
    
    if (!midiOut || !midiIn) {
      cerr << "Failed to set up midi" <<endl;
      return 1;
    }
    //	midiin->setCallback( &mycallback );
    midiIn->setCallback( midiCallback,(void *) this );
    // Don't ignore sysex, timing, or active sensing messages.
    midiIn->ignoreTypes( false, false, false );

    bcrIn->setCallback( midiCallback,(void *) this );
    
    bcr2000Driver.setMidiOut (midiOut);
    bcr2000Driver.initialiseBCR();

    return 0;
  }

  int setupAudio () {

    Stk::setSampleRate (sampleRate);
    try {
      //audioOut = shared_ptr<RtAudio> (new RtAudio(RtAudio::LINUX_ALSA));
      audioOut = shared_ptr<RtAudio> (new RtAudio());
    }catch  (RtError e){
      fprintf(stderr, "fail to create RtAudio: %s\n", e.what());
      return 1;
    }
    if (!audioOut){
      fprintf(stderr, "fail to allocate RtAudio\n");
      return 1;
    }
    /* probe audio devices */
    
    unsigned int devices = audioOut->getDeviceCount();
    // Scan through devices for various capabilities
    RtAudio::DeviceInfo info;
    for ( unsigned int i=0; i<devices; i++ ) {
      info = audioOut->getDeviceInfo( i );
      if ( info.probed == true ) {
	// Print, for example, the maximum number of output channels for each device
	std::cout << "device = " << i;
	std::cout << ": maximum output channels = " << info.outputChannels << "\n";
      }
    }
    unsigned int devId = audioOut->getDefaultOutputDevice();

    RtAudio::StreamParameters outParam;
    RtAudioFormat format = ( sizeof(StkFloat) == 8 ) ? RTAUDIO_FLOAT64 : RTAUDIO_FLOAT32;

    outParam.deviceId = devId;
    outParam.nChannels = numChannels;

    //    boost::function<int (void*,void*,unsigned int, double, unsigned int, void *)> fun =
    //      boost::bind (&MainApplication::rtaudio_callback, this, _1,_2,_3,_4,_5,_6);
    
    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_HOG_DEVICE;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    //    options.flags |= RTAUDIO_INTERLEAVED;

    audioOut->openStream(&outParam, NULL, format , sampleRate,
			 &bufsize, 
			 rtaudio_callback, 
			 this,
			 &options
			 );
    
    return 0;

  }
  

  

};


ofstream outlog("out.log");


int rtaudio_callback(
		     void			*outbuf,
		     void			*inbuf,
		     unsigned int		nFrames,
		     double			streamtime,
		     RtAudioStreamStatus	status,
		     void * data
		     )
{
  (void)inbuf;
  
  mainlog << "audio " << streamtime << ", " << status << endl;

  MainApplication * mainapp = (MainApplication *)data;
  
  //  StkFloat tempbuffer[nFrames*mainapp->numChannels];
  //  for (unsigned int i = 0; i<nFrames*mainapp->numChannels; i++) {
  //    tempbuffer[i] = 0;
    //  }


  typedef pair<RBufferWithMutex,RPolySynthesiser> RBuffSynthPair;

  vector<RBuffSynthPair> buffersandsynths;
  boost::thread_group group;

  RPhonesBuffersList phonesBuffers (new PhonesBuffersList);

  for (auto synthmidichannelpair:mainapp->synths) {

    RPolySynthesiser rsynth = synthmidichannelpair.second;
    if (rsynth) {

      RBufferWithMutex buffer (new BufferWithMutex<>
			       (
				mainapp->timePoint,
				mainapp->sampleRate,
				nFrames,
				mainapp->numChannels
				)
			     );
      buffersandsynths.push_back(RBuffSynthPair(buffer,rsynth));
      
      for (auto phone:rsynth->getPhones()) {
	phonesBuffers->addPair (phone,buffer);
      }
      
      
      //      auto bindoperation = boost::bind (&PolySynthesiser::playToBuffer, rsynth, buffer);
      //      group.create_thread  (bindoperation);
    }
  }

#if 1
  ThreadedPhonePlayer phonePlayer1 (phonesBuffers);
  ThreadedPhonePlayer phonePlayer2 (phonesBuffers);
  ThreadedPhonePlayer phonePlayer3 (phonesBuffers);
  ThreadedPhonePlayer phonePlayer4 (phonesBuffers);
  group.create_thread(boost::bind (&ThreadedPhonePlayer::threadedFunction,&phonePlayer1));
  group.create_thread(boost::bind (&ThreadedPhonePlayer::threadedFunction,&phonePlayer2));
  group.create_thread(boost::bind (&ThreadedPhonePlayer::threadedFunction,&phonePlayer3));
  group.create_thread(boost::bind (&ThreadedPhonePlayer::threadedFunction,&phonePlayer4));
  group.join_all();
#else
  ThreadedPhonePlayer phonePlayer1 (phonesBuffers);
  
  phonePlayer1.threadedFunction();
#endif
    

  mainapp->timePoint += (double)nFrames/mainapp->sampleRate;

  //  cout << buffer->numberOfChannels << " " 
  //       << buffer->numberOfFrames << " " 
  //       << outbuf << endl;

  int numberOfChannels = mainapp->numChannels;

  //  cout << "buffer done" << endl;
  int counter = 0;
  for (auto bufferandsynth: buffersandsynths) {
    RBufferWithMutex buffer = bufferandsynth.first;
    RPolySynthesiser synth = bufferandsynth.second;

    synth->fx.updateFXFromControllers();

    double volume = synth->controlSet->controllerNames["masterVolume"]->getLevel();
    double pan = synth->controlSet->controllerNames["pan"]->getLevel();

    safeVector<StkFloat> & synthbuf = buffer->getOutBufRef();
    StkFloat * outsamples = (StkFloat *)outbuf;
    for (unsigned int i=0;i<nFrames*numberOfChannels; i+=numberOfChannels) {
      double lval = synthbuf[i];
      double rval = lval;
      if (numberOfChannels > 1) 
	rval = synthbuf[i+1];

      double cmix =synth->fx.chorusMix->getLevel(); 
      if (cmix > 0.0) {
	double inval = (rval+lval)/2.0;
	synth->fx.chorus.tick(inval);
	const StkFrames& samples = synth->fx.chorus.lastFrame();
	
	lval = (1-cmix)*lval+cmix*samples[0];
	rval = (1-cmix)*rval+cmix*samples[1];
      }

      double emix = synth->fx.echoMix->getLevel();
      if (emix >0.0) {
	double inval = (rval+lval)/2.0;
	inval += synth->fx.echo.lastFrame()[0]*synth->fx.echoFeedback->getLevel();
	synth->fx.echo.tick(inval);
      
	const StkFrames& samples = synth->fx.echo.lastFrame();

	lval = (1-emix)*lval+emix*samples[0];
	rval = (1-emix)*rval+emix*samples[0];
      }

      double rmix = synth->fx.reverbMix->getLevel();
      if (rmix > 0.0) {
	synth->fx.reverb.tick(rval,lval);
	const StkFrames& samples = synth->fx.reverb.lastFrame();
	lval = (1-rmix)*lval+rmix*samples[0];
	rval = (1-rmix)*rval+rmix*samples[1];
      }

      lval = lval*volume*(1.0-pan)*2.0;
      rval = rval*volume*pan*2.0;

      if (counter == 0) {
	outsamples[i] = lval;
	if (numberOfChannels>1) 
	  outsamples[i+1] = rval;
      }
      else {
	outsamples[i] += lval;
	if (numberOfChannels>1) 
	  outsamples[i+1] += rval;
      }

     

    }
    counter ++;
  }
  
 
  //  outlog << streamtime << endl;

  return 0;
}

void midiCallback( double deltatime, std::vector< unsigned char > *message, void *classptr )
{
  unsigned int nBytes = message->size();
  int byte0 = 0;
  int byte1 = 0;
  int byte2 = 0;

  for ( unsigned int i=0; i<nBytes; i++ ) {
    //    std::cout << "Byte " << i << " = " << (int)message->at(i) << ", ";
    if (i == 0)
      byte0 = (int)message->at(i);
    if (i == 1)
      byte1 = (int)message->at(i);
    if (i == 2)
      byte2 = (int)message->at(i);
  }
 
  //  if ( nBytes > 0 )
  //    std::cout << "stamp = " << deltatime << std::endl;

  MainApplication * myclass = (MainApplication *)classptr;

  //  if (byte0 == 176 && byte1 == 101)
  //    byte1 = 102;

  if (byte0 > 143 && byte0<160) {
    int channelNumber = byte0-144;
    if (myclass->synths[channelNumber]){
      if (byte2 == 0)
	myclass->synths[channelNumber]->noteOff (byte1,byte2,myclass->timePoint);
      else 
	myclass->synths[channelNumber]->noteOn (byte1,byte2,myclass->timePoint);
    }
  }
  else if (byte0 > 127 && byte0<144) {
    int channelNumber = byte0-128;
    if (myclass->synths[channelNumber])
      myclass->synths[channelNumber]->noteOff (byte1,byte2,myclass->timePoint);
  }
  else {
    ControlVariable cvar(byte0,byte1);
    bcr2000Driver.interpretControlMessage(cvar,byte2,myclass->focalSynth);
  }
}


int
main(void)
{

  MainApplication app;
  app.init();
  app.run();


  return 0;
}





