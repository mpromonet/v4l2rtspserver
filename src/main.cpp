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

#include <sstream>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

// project
#include "logger.h"

#include "V4l2ReadCapture.h"
#include "V4l2MmapCapture.h"

#include "H264_V4l2DeviceSource.h"
#include "ServerMediaSubsession.h"

// -----------------------------------------
//    signal handler
// -----------------------------------------
char quit = 0;
void sighandler(int n)
{ 
	printf("SIGINT\n");
	quit =1;
}

// -----------------------------------------
//    add an RTSP session
// -----------------------------------------
void addSession(RTSPServer* rtspServer, const char* sessionName, ServerMediaSubsession *subSession)
{
	UsageEnvironment& env(rtspServer->envir());
	ServerMediaSession* sms = ServerMediaSession::createNew(env, sessionName);
	sms->addSubsession(subSession);
	rtspServer->addServerMediaSession(sms);

	char* url = rtspServer->rtspURL(sms);
	LOG(NOTICE) << "Play this stream using the URL \"" << url << "\"";
	delete[] url;			
}

// -----------------------------------------
//    create video capture interface
// -----------------------------------------
V4l2Capture* createVideoCapure(const V4L2DeviceParameters & param, bool useMmap)
{
	V4l2Capture* videoCapture = NULL;
	if (useMmap)
	{
		videoCapture = V4l2MmapCapture::createNew(param);
	}
	else
	{
		videoCapture = V4l2ReadCapture::createNew(param);
	}
	return videoCapture;
}
	
// -----------------------------------------
//    entry point
// -----------------------------------------
int main(int argc, char** argv) 
{
	// default parameters
	const char *dev_name = "/dev/video0";	
	int format = V4L2_PIX_FMT_H264;
	int width = 640;
	int height = 480;
	int queueSize = 10;
	int fps = 25;
	unsigned short rtpPortNum = 20000;
	unsigned short rtcpPortNum = rtpPortNum+1;
	unsigned char ttl = 5;
	struct in_addr destinationAddress;
	unsigned short rtspPort = 8554;
	unsigned short rtspOverHTTPPort = 0;
	bool multicast = false;
	int verbose = 0;
	std::string outputFile;
	bool useMmap = true;
	std::string url = "unicast";
	std::string murl = "multicast";
	bool useThread = true;
	in_addr_t maddr = INADDR_NONE;
	bool repeatConfig = true;
	int timeout = 65;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:" "I:P:T:m:u:M:ct:" "rsF:W:H:" "h")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose = 1; if (optarg && *optarg=='v') verbose++;  break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'O':	outputFile = optarg; break;
			// RTSP/RTP
			case 'I':       ReceivingInterfaceAddr = inet_addr(optarg); break;
			case 'P':	rtspPort = atoi(optarg); break;
			case 'T':	rtspOverHTTPPort = atoi(optarg); break;
			case 'u':	url = optarg; break;
			case 'm':	multicast = true; murl = optarg; break;
			case 'M':	multicast = true; maddr = inet_addr(optarg); break;
			case 'c':	repeatConfig = false; break;
			case 't':	timeout = atoi(optarg); break;
			// V4L2
			case 'r':	useMmap =  false; break;
			case 's':	useThread =  false; break;
			case 'F':	fps = atoi(optarg); break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;

			case 'h':
			default:
			{
				std::cout << argv[0] << " [-v[v]][-m] [-P RTSP port][-P RTSP/HTTP port][-Q queueSize] [-M] [-t] [-W width] [-H height] [-F fps] [-O file] [device]" << std::endl;
				std::cout << "\t -v       : verbose"                                                               << std::endl;
				std::cout << "\t -vv      : very verbose"                                                          << std::endl;
				std::cout << "\t -Q length: Number of frame queue  (default "<< queueSize << ")"                   << std::endl;
				std::cout << "\t -O output: Copy captured frame to a file or a V4L2 device"                        << std::endl;
				std::cout << "\t RTSP options :"                                                                   << std::endl;
				std::cout << "\t -I addr  : RTSP interface (default autodetect)"                                   << std::endl;
				std::cout << "\t -P port  : RTSP port (default "<< rtspPort << ")"                                 << std::endl;
				std::cout << "\t -T port  : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")"               << std::endl;
				std::cout << "\t -u url   : unicast url (default " << url << ")"                                   << std::endl;
				std::cout << "\t -m url   : multicast url (default " << murl << ")"                                << std::endl;
				std::cout << "\t -M addr  : multicast group (default is a random address)"                         << std::endl;
				std::cout << "\t -c       : don't repeat config (default repeat config before IDR frame)"          << std::endl;
				std::cout << "\t -t secs  : RTCP expiration timeout (default " << timeout << ")"                   << std::endl;
				std::cout << "\t V4L2 options :"                                                                   << std::endl;
				std::cout << "\t -r       : V4L2 capture using read interface (default use memory mapped buffers)" << std::endl;
				std::cout << "\t -s       : V4L2 capture using live555 mainloop (default use a reader thread)"     << std::endl;
				std::cout << "\t -F fps   : V4L2 capture framerate (default "<< fps << ")"                         << std::endl;
				std::cout << "\t -W width : V4L2 capture width (default "<< width << ")"                           << std::endl;
				std::cout << "\t -H height: V4L2 capture height (default "<< height << ")"                         << std::endl;
				std::cout << "\t device   : V4L2 capture device (default "<< dev_name << ")"                       << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
	// init logger
	initLogger(verbose);
     
	// create live555 environment
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	
	
	// create RTSP server
	UserAuthenticationDatabase* authDB = NULL;
	RTSPServer* rtspServer = RTSPServer::createNew(*env, rtspPort, authDB, timeout);
	if (rtspServer == NULL) 
	{
		LOG(ERROR) << "Failed to create RTSP server: " << env->getResultMsg();
	}
	else
	{
		// set http tunneling
		if (rtspOverHTTPPort)
		{
			rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
		}
		
		// Init capture
		LOG(NOTICE) << "Create V4L2 Source..." << dev_name;
		V4L2DeviceParameters param(dev_name,format,width,height,fps, verbose);
		V4l2Capture* videoCapture = createVideoCapure(param, useMmap);
		if (videoCapture)
		{
			V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0,verbose);
			V4l2Output out(outparam);
			
			LOG(NOTICE) << "Start V4L2 Capture..." << dev_name;
			videoCapture->captureStart();
			V4L2DeviceSource* videoES =  H264_V4L2DeviceSource::createNew(*env, param, videoCapture, out.getFd(), queueSize, useThread, repeatConfig);
			if (videoES == NULL) 
			{
				LOG(FATAL) << "Unable to create source for device " << dev_name;
			}
			else
			{
				OutPacketBuffer::maxSize = videoCapture->getBufferSize();
				StreamReplicator* replicator = StreamReplicator::createNew(*env, videoES, false);

				// Create Server Multicast Session
				if (multicast)
				{
					if (maddr == INADDR_NONE) maddr = chooseRandomIPv4SSMAddress(*env);	
					destinationAddress.s_addr = maddr;
					LOG(NOTICE) << "RTP  address " << inet_ntoa(destinationAddress) << ":" << rtpPortNum;
					LOG(NOTICE) << "RTCP address " << inet_ntoa(destinationAddress) << ":" << rtcpPortNum;
					addSession(rtspServer, murl.c_str(), MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, replicator,format));
				
				}
				// Create Server Unicast Session
				addSession(rtspServer, url.c_str(), UnicastServerMediaSubsession::createNew(*env,replicator,format));

				// main loop
				signal(SIGINT,sighandler);
				env->taskScheduler().doEventLoop(&quit); 
				LOG(NOTICE) << "Exiting....";			
				Medium::close(videoES);
			}			
			videoCapture->captureStop();			
			delete videoCapture;
		}
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;	
	
	return 0;
}



