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

#include "V4l2Device.h"
#include "V4l2Capture.h"
#include "V4l2Output.h"

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
void addSession(RTSPServer* rtspServer, const std::string & sessionName, ServerMediaSubsession *subSession)
{
	UsageEnvironment& env(rtspServer->envir());
	ServerMediaSession* sms = ServerMediaSession::createNew(env, sessionName.c_str());
	if (sms != NULL)
	{
		sms->addSubsession(subSession);
		rtspServer->addServerMediaSession(sms);

		char* url = rtspServer->rtspURL(sms);
		if (url != NULL)
		{
			LOG(NOTICE) << "Play this stream using the URL \"" << url << "\"";
			delete[] url;			
		}
	}
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
	unsigned short rtspPort = 8554;
	unsigned short rtspOverHTTPPort = 0;
	bool multicast = false;
	int verbose = 0;
	std::string outputFile;
	bool useMmap = true;
	std::string url = "unicast";
	std::string murl = "multicast";
	bool useThread = true;
	std::string maddr;
	bool repeatConfig = true;
	int timeout = 65;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:" "I:P:T:m:u:M:ct:" "rsfF:W:H:" "h")) != -1)
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
			case 'M':	multicast = true; maddr = optarg; break;
			case 'c':	repeatConfig = false; break;
			case 't':	timeout = atoi(optarg); break;
			// V4L2
			case 'r':	useMmap =  false; break;
			case 's':	useThread =  false; break;
			case 'f':	format = 0; break;
			case 'F':	fps = atoi(optarg); break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;

			case 'h':
			default:
			{
				std::cout << argv[0] << " [-v[v]] [-Q queueSize] [-O file]"                                        << std::endl;
				std::cout << "\t          [-I interface] [-P RTSP port] [-T RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout]" << std::endl;
				std::cout << "\t          [-r] [-s] [-W width] [-H height] [-F fps] [device] [device]"           << std::endl;
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
				std::cout << "\t -M addr  : multicast group:port (default is random_address:20000)"                << std::endl;
				std::cout << "\t -c       : don't repeat config (default repeat config before IDR frame)"          << std::endl;
				std::cout << "\t -t secs  : RTCP expiration timeout (default " << timeout << ")"                   << std::endl;
				std::cout << "\t V4L2 options :"                                                                   << std::endl;
				std::cout << "\t -r       : V4L2 capture using read interface (default use memory mapped buffers)" << std::endl;
				std::cout << "\t -s       : V4L2 capture using live555 mainloop (default use a reader thread)"     << std::endl;
				std::cout << "\t -f       : V4L2 capture using current format (-W,-H,-F are ignore)"               << std::endl;
				std::cout << "\t -W width : V4L2 capture width (default "<< width << ")"                           << std::endl;
				std::cout << "\t -H height: V4L2 capture height (default "<< height << ")"                         << std::endl;
				std::cout << "\t -F fps   : V4L2 capture framerate (default "<< fps << ")"                         << std::endl;
				std::cout << "\t device   : V4L2 capture device (default "<< dev_name << ")"                       << std::endl;
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

	// init logger
	initLogger(verbose);
     
	// create live555 environment
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	

	// split multicast info
	std::istringstream is(maddr);
	std::string ip;
	getline(is, ip, ':');						
	struct in_addr destinationAddress;
	destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);
	if (!ip.empty())
	{
		destinationAddress.s_addr = inet_addr(ip.c_str());
	}						
	
	std::string port;
	getline(is, port, ':');						
	unsigned short rtpPortNum = 20000;
	if (!port.empty())
	{
		rtpPortNum = atoi(port.c_str());
	}	
	unsigned short rtcpPortNum = rtpPortNum+1;
	unsigned char ttl = 5;
	
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

				
		std::list<std::string>::iterator devIt;
		for ( devIt=devList.begin() ; devIt!=devList.end() ; ++devIt)
		{
			std::string deviceName(*devIt);
			
			// Init capture
			LOG(NOTICE) << "Create V4L2 Source..." << deviceName;
			V4L2DeviceParameters param(deviceName.c_str(),format,width,height,fps, verbose);
			V4l2Capture* videoCapture = V4l2DeviceFactory::CreateVideoCapure(param, useMmap);
			if (videoCapture)
			{
				format = videoCapture->getFormat();
				V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0,verbose);
				V4l2Output out(outparam);
				
				LOG(NOTICE) << "Start V4L2 Capture..." << deviceName;
				if (!videoCapture->captureStart())
				{
					LOG(NOTICE) << "Cannot start V4L2 Capture for:" << deviceName;
				}
				V4L2DeviceSource* videoES = NULL;
				if (format == V4L2_PIX_FMT_H264)
				{
					videoES = H264_V4L2DeviceSource::createNew(*env, param, videoCapture, out.getFd(), queueSize, useThread, repeatConfig);
				}
				else
				{
					videoES = V4L2DeviceSource::createNew(*env, param, videoCapture, out.getFd(), queueSize, useThread);
				}
				if (videoES == NULL) 
				{
					LOG(FATAL) << "Unable to create source for device " << deviceName;
					delete videoCapture;
				}
				else
				{	
					// extend buffer size if needed
					if (videoCapture->getBufferSize() > OutPacketBuffer::maxSize)
					{
						OutPacketBuffer::maxSize = videoCapture->getBufferSize();
					}
					
					StreamReplicator* replicator = StreamReplicator::createNew(*env, videoES, false);
					
					std::string baseUrl;
					if (devList.size() > 1)
					{
						baseUrl = basename(deviceName.c_str());
						baseUrl.append("/");
					}
					
					// Create Multicast Session
					if (multicast)						
					{		
						LOG(NOTICE) << "RTP  address " << inet_ntoa(destinationAddress) << ":" << rtpPortNum;
						LOG(NOTICE) << "RTCP address " << inet_ntoa(destinationAddress) << ":" << rtcpPortNum;
						addSession(rtspServer, baseUrl+murl, MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, replicator,format));					
						
						// increment ports for next sessions
						rtpPortNum+=2;
						rtcpPortNum+=2;
						
					}
					// Create Unicast Session
					addSession(rtspServer, baseUrl+url, UnicastServerMediaSubsession::createNew(*env,replicator,format));
				}			
			}
		}

		// main loop
		signal(SIGINT,sighandler);
		env->taskScheduler().doEventLoop(&quit); 
		LOG(NOTICE) << "Exiting....";			
		
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;	
	
	return 0;
}



