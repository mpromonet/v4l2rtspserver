/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** main.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** H264 capture using V4L2                                                            
** RTSP using live555                                                                 
**                                                                                    
** -------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <dirent.h>

#include <sstream>

// libv4l2
#include <linux/videodev2.h>

// project
#include "logger.h"

#include "V4l2Device.h"
#include "V4l2Capture.h"
#include "V4l2Output.h"

#include "DeviceSourceFactory.h"
#include "V4l2RTSPServer.h"

#ifdef HAVE_ALSA
#include "ALSACapture.h"
#endif

// -----------------------------------------
//    signal handler
// -----------------------------------------
char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}

// -------------------------------------------------------
//    decode multicast url <group>:<rtp_port>:<rtcp_port>
// -------------------------------------------------------
void decodeMulticastUrl(const std::string & maddr, in_addr & destinationAddress, unsigned short & rtpPortNum, unsigned short & rtcpPortNum)
{
	std::istringstream is(maddr);
	std::string ip;
	getline(is, ip, ':');						
	if (!ip.empty())
	{
		destinationAddress.s_addr = inet_addr(ip.c_str());
	}						
	
	std::string port;
	getline(is, port, ':');						
	rtpPortNum = 20000;
	if (!port.empty())
	{
		rtpPortNum = atoi(port.c_str());
	}	
	rtcpPortNum = rtpPortNum+1;
}

// -------------------------------------------------------
//    split video,audio device
// -------------------------------------------------------
void decodeDevice(const std::string & device, std::string & videoDev, std::string & audioDev)
{
	std::istringstream is(device);
	getline(is, videoDev, ',');						
	getline(is, audioDev);						
}

std::string getDeviceName(const std::string & devicePath)
{
	std::string deviceName(devicePath);
	size_t pos = deviceName.find_last_of('/');
	if (pos != std::string::npos) {
		deviceName.erase(0,pos+1);
	}
	return deviceName;
}

#ifdef HAVE_ALSA
snd_pcm_format_t decodeAudioFormat(const std::string& fmt)
{
	snd_pcm_format_t audioFmt = SND_PCM_FORMAT_UNKNOWN;
	if (fmt == "S16_BE") {
		audioFmt = SND_PCM_FORMAT_S16_BE;
	} else if (fmt == "S16_LE") {
		audioFmt = SND_PCM_FORMAT_S16_LE;
	} else if (fmt == "S24_BE") {
		audioFmt = SND_PCM_FORMAT_S24_BE;
	} else if (fmt == "S24_LE") {
		audioFmt = SND_PCM_FORMAT_S24_LE;
	} else if (fmt == "S32_BE") {
		audioFmt = SND_PCM_FORMAT_S32_BE;
	} else if (fmt == "S32_LE") {
		audioFmt = SND_PCM_FORMAT_S32_LE;
	} else if (fmt == "ALAW") {
		audioFmt = SND_PCM_FORMAT_A_LAW;
	} else if (fmt == "MULAW") {
		audioFmt = SND_PCM_FORMAT_MU_LAW;
	} else if (fmt == "S8") {
		audioFmt = SND_PCM_FORMAT_S8;
	} else if (fmt == "MPEG") {
		audioFmt = SND_PCM_FORMAT_MPEG;
	}
	return audioFmt;
}

/* ---------------------------------------------------------------------------
**  get a "deviceid" from uevent sys file
** -------------------------------------------------------------------------*/
std::string getDeviceId(const std::string& evt) {
    std::string deviceid;
    std::istringstream f(evt);
    std::string key;
    while (getline(f, key, '=')) {
            std::string value;
	    if (getline(f, value)) {
		    if ( (key =="PRODUCT") || (key == "PCI_SUBSYS_ID") ) {
			    deviceid = value;
			    break;
		    }
	    }
    }
    return deviceid;
}

std::string  getV4l2Alsa(const std::string& v4l2device) {
	std::string audioDevice(v4l2device);
	
	std::map<std::string,std::string> videodevices;
	std::string video4linuxPath("/sys/class/video4linux");
	DIR *dp = opendir(video4linuxPath.c_str());
	if (dp != NULL) {
		struct dirent *entry = NULL;
		while((entry = readdir(dp))) {
			std::string devicename;
			std::string deviceid;
			if (strstr(entry->d_name,"video") == entry->d_name) {
				std::string ueventPath(video4linuxPath);
				ueventPath.append("/").append(entry->d_name).append("/device/uevent");
				std::ifstream ifsd(ueventPath.c_str());
				deviceid = std::string(std::istreambuf_iterator<char>{ifsd}, {});
				deviceid.erase(deviceid.find_last_not_of("\n")+1);
			}

			if (!deviceid.empty()) {
				videodevices[entry->d_name] = getDeviceId(deviceid);
			}
		}
		closedir(dp);
	}

	std::map<std::string,std::string> audiodevices;
	int rcard = -1;
	while ( (snd_card_next(&rcard) == 0) && (rcard>=0) ) {
		void **hints = NULL;
		if (snd_device_name_hint(rcard, "pcm", &hints) >= 0) {
			void **str = hints;
			while (*str) {				
				std::ostringstream os;
				os << "/sys/class/sound/card" << rcard << "/device/uevent";

				std::ifstream ifs(os.str().c_str());
				std::string deviceid = std::string(std::istreambuf_iterator<char>{ifs}, {});
				deviceid.erase(deviceid.find_last_not_of("\n")+1);
				deviceid = getDeviceId(deviceid);

				if (!deviceid.empty()) {
					if (audiodevices.find(deviceid) == audiodevices.end()) {
						std::string audioname = snd_device_name_get_hint(*str, "NAME");
						audiodevices[deviceid] = audioname;
					}
				}

				str++;
			}

			snd_device_name_free_hint(hints);
		}
	}

	auto deviceId  = videodevices.find(getDeviceName(v4l2device));
	if (deviceId != videodevices.end()) {
		auto audioDeviceIt = audiodevices.find(deviceId->second);
		
		if (audioDeviceIt != audiodevices.end()) {
			audioDevice = audioDeviceIt->second;
			std::cout <<  v4l2device << "=>" << audioDevice << std::endl;			
		}
	}
	
	
	return audioDevice;
}
#endif

// -----------------------------------------
//    entry point
// -----------------------------------------
int main(int argc, char** argv) 
{
	// default parameters
	const char *dev_name = "/dev/video0,/dev/video0";	
	unsigned int format = ~0;
	std::list<unsigned int> videoformatList;
	int width = 0;
	int height = 0;
	int queueSize = 10;
	int fps = 25;
	unsigned short rtspPort = 8554;
	unsigned short rtspOverHTTPPort = 0;
	bool multicast = false;
	int verbose = 0;
	std::string outputFile;
	V4l2Access::IoType ioTypeIn  = V4l2Access::IOTYPE_MMAP;
	V4l2Access::IoType ioTypeOut = V4l2Access::IOTYPE_MMAP;
	int openflags = O_RDWR | O_NONBLOCK; 
	std::string url = "unicast";
	std::string murl = "multicast";
	std::string tsurl = "ts";
	bool useThread = true;
	std::string maddr;
	bool repeatConfig = true;
	int timeout = 65;
	int defaultHlsSegment = 2;
	unsigned int hlsSegment = 0;
	const char* realm = NULL;
	std::list<std::string> userPasswordList;
	std::string webroot;
#ifdef HAVE_ALSA	
	int audioFreq = 44100;
	int audioNbChannels = 2;
	std::list<snd_pcm_format_t> audioFmtList;
	snd_pcm_format_t audioFmt = SND_PCM_FORMAT_UNKNOWN;
#endif	
	const char* defaultPort = getenv("PORT");
	if (defaultPort != NULL) {
		rtspPort = atoi(defaultPort);
	}

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:b:" "I:P:p:m::u:M:ct:S::" "R:U:" "rwBsf::F:W:H:G:" "A:C:a:" "Vh")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose    = 1; if (optarg && *optarg=='v') verbose++;  break;
			case 'Q':	queueSize  = atoi(optarg); break;
			case 'O':	outputFile = optarg; break;
			case 'b':	webroot = optarg; break;
			
			// RTSP/RTP
			case 'I':       ReceivingInterfaceAddr  = inet_addr(optarg); break;
			case 'P':	rtspPort                = atoi(optarg); break;
			case 'p':	rtspOverHTTPPort        = atoi(optarg); break;
			case 'u':	url                     = optarg; break;
			case 'm':	multicast = true; murl  = optarg ? optarg : murl; break;
			case 'M':	multicast = true; maddr = optarg; break;
			case 'c':	repeatConfig            = false; break;
			case 't':	timeout                 = atoi(optarg); break;
			case 'S':	hlsSegment              = optarg ? atoi(optarg) : defaultHlsSegment; break;
			
			// users
			case 'R':       realm                   = optarg; break;
			case 'U':       userPasswordList.push_back(optarg); break;
			
			// V4L2
			case 'r':	ioTypeIn  = V4l2Access::IOTYPE_READWRITE; break;
			case 'w':	ioTypeOut = V4l2Access::IOTYPE_READWRITE; break;	
			case 'B':	openflags = O_RDWR; break;	
			case 's':	useThread =  false; break;
			case 'f':	format    = V4l2Device::fourcc(optarg); if (format) {videoformatList.push_back(format);};  break;
			case 'F':	fps       = atoi(optarg); break;
			case 'W':	width     = atoi(optarg); break;
			case 'H':	height    = atoi(optarg); break;
			case 'G':   sscanf(optarg,"%dx%dx%d", &width, &height, &fps); break;
			
			// ALSA
#ifdef HAVE_ALSA	
			case 'A':	audioFreq = atoi(optarg); break;
			case 'C':	audioNbChannels = atoi(optarg); break;
			case 'a':	audioFmt = decodeAudioFormat(optarg); if (audioFmt != SND_PCM_FORMAT_UNKNOWN) {audioFmtList.push_back(audioFmt);} ; break;
#endif			
			
			// version
			case 'V':	
				std::cout << VERSION << std::endl;
				exit(0);			
			break;
			
			// help
			case 'h':
			default:
			{
				std::cout << argv[0] << " [-v[v]] [-Q queueSize] [-O file]"                                        << std::endl;
				std::cout << "\t          [-I interface] [-P RTSP port] [-p RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] [-T] [-S[duration]]" << std::endl;
				std::cout << "\t          [-r] [-w] [-s] [-f[format] [-W width] [-H height] [-F fps] [device] [device]"                        << std::endl;
				std::cout << "\t -v               : verbose"                                                                                          << std::endl;
				std::cout << "\t -vv              : very verbose"                                                                                     << std::endl;
				std::cout << "\t -Q <length>      : Number of frame queue  (default "<< queueSize << ")"                                              << std::endl;
				std::cout << "\t -O <output>      : Copy captured frame to a file or a V4L2 device"                                                   << std::endl;
				std::cout << "\t -b <webroot>     : path to webroot" << std::endl;
				
				std::cout << "\t RTSP/RTP options"                                                                                           << std::endl;
				std::cout << "\t -I <addr>        : RTSP interface (default autodetect)"                                                              << std::endl;
				std::cout << "\t -P <port>        : RTSP port (default "<< rtspPort << ")"                                                            << std::endl;
				std::cout << "\t -p <port>        : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")"                                          << std::endl;
				std::cout << "\t -U <user>:<pass> : RTSP user and password"                                                                    << std::endl;
				std::cout << "\t -R <realm>       : use md5 password 'md5(<username>:<realm>:<password>')"                                            << std::endl;
				std::cout << "\t -u <url>         : unicast url (default " << url << ")"                                                              << std::endl;
				std::cout << "\t -m <url>         : multicast url (default " << murl << ")"                                                           << std::endl;
				std::cout << "\t -M <addr>        : multicast group:port (default is random_address:20000)"                                           << std::endl;
				std::cout << "\t -c               : don't repeat config (default repeat config before IDR frame)"                                     << std::endl;
				std::cout << "\t -t <timeout>     : RTCP expiration timeout in seconds (default " << timeout << ")"                                   << std::endl;
				std::cout << "\t -S[<duration>]   : enable HLS & MPEG-DASH with segment duration  in seconds (default " << defaultHlsSegment << ")" << std::endl;
				
				std::cout << "\t V4L2 options"                                                                                               << std::endl;
				std::cout << "\t -r               : V4L2 capture using read interface (default use memory mapped buffers)"                            << std::endl;
				std::cout << "\t -w               : V4L2 capture using write interface (default use memory mapped buffers)"                           << std::endl;
				std::cout << "\t -B               : V4L2 capture using blocking mode (default use non-blocking mode)"                                 << std::endl;
				std::cout << "\t -s               : V4L2 capture using live555 mainloop (default use a reader thread)"                                << std::endl;
				std::cout << "\t -f               : V4L2 capture using current capture format (-W,-H,-F are ignored)"                                 << std::endl;
				std::cout << "\t -f<format>       : V4L2 capture using format (-W,-H,-F are used)"                                                    << std::endl;
				std::cout << "\t -W <width>       : V4L2 capture width (default "<< width << ")"                                                      << std::endl;
				std::cout << "\t -H <height>      : V4L2 capture height (default "<< height << ")"                                                    << std::endl;
				std::cout << "\t -F <fps>         : V4L2 capture framerate (default "<< fps << ")"                                                    << std::endl;
				std::cout << "\t -G <w>x<h>[x<f>] : V4L2 capture format (default "<< width << "x" << height << "x" << fps << ")"  << std::endl;
				
#ifdef HAVE_ALSA	
				std::cout << "\t ALSA options"                                                                                               << std::endl;
				std::cout << "\t -A freq          : ALSA capture frequency and channel (default " << audioFreq << ")"                                << std::endl;
				std::cout << "\t -C channels      : ALSA capture channels (default " << audioNbChannels << ")"                                       << std::endl;
				std::cout << "\t -a fmt           : ALSA capture audio format (default S16_BE)"                                                      << std::endl;
#endif
				
				std::cout << "\t Devices :"                                                                                                    << std::endl;
				std::cout << "\t [V4L2 device][,ALSA device] : V4L2 capture device or/and ALSA capture device (default "<< dev_name << ")"     << std::endl;
				exit(0);
			}
		}
	}
	std::list<std::string> devList;
	while (optind<argc)
	{
		devList.push_back(argv[optind]);
		optind++;
	}
	if (devList.empty())
	{
		devList.push_back(dev_name);
	}
	
	// default format tries
	if ((videoformatList.empty()) && (format!=0)) {
		videoformatList.push_back(V4L2_PIX_FMT_H264);
		videoformatList.push_back(V4L2_PIX_FMT_MJPEG);
		videoformatList.push_back(V4L2_PIX_FMT_JPEG);
	}

#ifdef HAVE_ALSA	
	// default audio format tries
	if (audioFmtList.empty()) {
		audioFmtList.push_back(SND_PCM_FORMAT_S16_LE);
		audioFmtList.push_back(SND_PCM_FORMAT_S16_BE);
	}
#endif	
	
	// init logger
	initLogger(verbose);
	LOG(NOTICE) << "Version: " << VERSION << " live555 version:" << LIVEMEDIA_LIBRARY_VERSION_STRING;
     	
	
	// create RTSP server
	V4l2RTSPServer rtspServer(rtspPort, rtspOverHTTPPort, timeout, hlsSegment, userPasswordList, realm, webroot);
	if (!rtspServer.available()) 
	{
		LOG(ERROR) << "Failed to create RTSP server: " << rtspServer.getResultMsg();
	}
	else
	{		
		// decode multicast info
		struct in_addr destinationAddress;
		destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*rtspServer.env());
		unsigned short rtpPortNum = 20000;
		unsigned short rtcpPortNum = rtpPortNum+1;
		unsigned char ttl = 5;
		decodeMulticastUrl(maddr, destinationAddress, rtpPortNum, rtcpPortNum);	

		V4l2Output* out = NULL;
		int nbSource = 0;
		std::list<std::string>::iterator devIt;
		for ( devIt=devList.begin() ; devIt!=devList.end() ; ++devIt)
		{
			std::string deviceName(*devIt);
			
			std::string videoDev;
			std::string audioDev;
			decodeDevice(deviceName, videoDev, audioDev);
			
			std::string baseUrl;
			if (devList.size() > 1)
			{
				baseUrl = getDeviceName(videoDev);
				baseUrl.append("/");
			}	
			StreamReplicator* videoReplicator = NULL;
			std::string rtpVideoFormat;
			if (!videoDev.empty())
			{
				// Init video capture
				LOG(NOTICE) << "Create V4L2 Source..." << videoDev;
				
				V4L2DeviceParameters param(videoDev.c_str(), videoformatList, width, height, fps, verbose, openflags);
				V4l2Capture* videoCapture = V4l2Capture::create(param, ioTypeIn);
				if (videoCapture)
				{
					int outfd = -1;
					
					if (!outputFile.empty())
					{
						V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0,verbose);
						out = V4l2Output::create(outparam, ioTypeOut);
						if (out != NULL)
						{
							outfd = out->getFd();
						}
					}
					
					rtpVideoFormat.assign(V4l2RTSPServer::getVideoRtpFormat(videoCapture->getFormat()));
					if (rtpVideoFormat.empty()) {
						LOG(FATAL) << "No Streaming format supported for device " << videoDev;
						delete videoCapture;
					} else {
						LOG(NOTICE) << "Create Source ..." << videoDev;
						videoReplicator = DeviceSourceFactory::createStreamReplicator(rtspServer.env(), videoCapture->getFormat(), new DeviceCaptureAccess<V4l2Capture>(videoCapture), queueSize, useThread, outfd, repeatConfig);
						if (videoReplicator == NULL) 
						{
							LOG(FATAL) << "Unable to create source for device " << videoDev;
							delete videoCapture;
						}
					}
				}
			}
					
			// Init Audio Capture
			StreamReplicator* audioReplicator = NULL;
			std::string rtpAudioFormat;
#ifdef HAVE_ALSA
			if (!audioDev.empty())
			{
				// find the ALSA device associated with the V4L2 device
				audioDev = getV4l2Alsa(audioDev);
			
				// Init audio capture
				LOG(NOTICE) << "Create ALSA Source..." << audioDev;
				
				ALSACaptureParameters param(audioDev.c_str(), audioFmtList, audioFreq, audioNbChannels, verbose);
				ALSACapture* audioCapture = ALSACapture::createNew(param);
				if (audioCapture) 
				{
					rtpAudioFormat.assign(V4l2RTSPServer::getAudioRtpFormat(audioCapture->getFormat(),audioCapture->getSampleRate(), audioCapture->getChannels()));

					audioReplicator = DeviceSourceFactory::createStreamReplicator(rtspServer.env(), 0, new DeviceCaptureAccess<ALSACapture>(audioCapture), queueSize, useThread);
					if (audioReplicator == NULL) 
					{
						LOG(FATAL) << "Unable to create source for device " << audioDev;
						delete audioCapture;
					}
				}
			}		
#endif
					
										
			// Create Multicast Session
			if (multicast)						
			{		
				LOG(NOTICE) << "RTP  address " << inet_ntoa(destinationAddress) << ":" << rtpPortNum;
				LOG(NOTICE) << "RTCP address " << inet_ntoa(destinationAddress) << ":" << rtcpPortNum;
			
				std::list<ServerMediaSubsession*> subSession;						
				if (videoReplicator)
				{
					subSession.push_back(MulticastServerMediaSubsession::createNew(*rtspServer.env(), destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, videoReplicator, rtpVideoFormat));					
					// increment ports for next sessions
					rtpPortNum+=2;
					rtcpPortNum+=2;
				}
				
				if (audioReplicator)
				{
					subSession.push_back(MulticastServerMediaSubsession::createNew(*rtspServer.env(), destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, audioReplicator, rtpAudioFormat));				
					
					// increment ports for next sessions
					rtpPortNum+=2;
					rtcpPortNum+=2;
				}
				nbSource += rtspServer.addSession(baseUrl+murl, subSession);																
			}
			
			// Create HLS Session					
			if (hlsSegment > 0)
			{
				std::list<ServerMediaSubsession*> subSession;
				if (videoReplicator)
				{
					subSession.push_back(TSServerMediaSubsession::createNew(*rtspServer.env(), videoReplicator, rtpVideoFormat, audioReplicator, rtpAudioFormat, hlsSegment));				
				}
				nbSource += rtspServer.addSession(baseUrl+tsurl, subSession);
				
				struct in_addr ip;
				ip.s_addr = ourIPAddress(*rtspServer.env());
				LOG(NOTICE) << "HLS       http://" << inet_ntoa(ip) << ":" << rtspPort << "/" << baseUrl+tsurl << ".m3u8";
				LOG(NOTICE) << "MPEG-DASH http://" << inet_ntoa(ip) << ":" << rtspPort << "/" << baseUrl+tsurl << ".mpd";
			}
			
			// Create Unicast Session					
			std::list<ServerMediaSubsession*> subSession;
			if (videoReplicator)
			{
				subSession.push_back(UnicastServerMediaSubsession::createNew(*rtspServer.env(), videoReplicator, rtpVideoFormat));				
			}
			if (audioReplicator)
			{
				subSession.push_back(UnicastServerMediaSubsession::createNew(*rtspServer.env(), audioReplicator, rtpAudioFormat));				
			}
			nbSource += rtspServer.addSession(baseUrl+url, subSession);				
		}

		if (nbSource>0)
		{
			// main loop
			signal(SIGINT,sighandler);
			rtspServer.eventLoop(&quit); 
			LOG(NOTICE) << "Exiting....";			
		}

		if (out)
		{
			delete out;
		}
	}
	
	return 0;
}



