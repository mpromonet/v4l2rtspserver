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
#include "HTTPServer.h"
#include "ALSACapture.h"

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
//    create UserAuthenticationDatabase for RTSP server
// -----------------------------------------
UserAuthenticationDatabase* createUserAuthenticationDatabase(const std::list<std::string> & userPasswordList, const char* realm)
{
	UserAuthenticationDatabase* auth = NULL;
	if (userPasswordList.size() > 0)
	{
		auth = new UserAuthenticationDatabase(realm, (realm != NULL) );
		
		std::list<std::string>::const_iterator it;
		for (it = userPasswordList.begin(); it != userPasswordList.end(); ++it)
		{
			std::istringstream is(*it);
			std::string user;
			getline(is, user, ':');	
			std::string password;
			getline(is, password);	
			auth->addUserRecord(user.c_str(), password.c_str());
		}
	}
	
	return auth;
}

// -----------------------------------------
//    create RTSP server
// -----------------------------------------
RTSPServer* createRTSPServer(UsageEnvironment& env, unsigned short rtspPort, unsigned short rtspOverHTTPPort, int timeout, unsigned int hlsSegment, const std::list<std::string> & userPasswordList, const char* realm)
{
	UserAuthenticationDatabase* auth = createUserAuthenticationDatabase(userPasswordList, realm);
	RTSPServer* rtspServer = HTTPServer::createNew(env, rtspPort, auth, timeout, hlsSegment);
	if (rtspServer != NULL)
	{
		// set http tunneling
		if (rtspOverHTTPPort)
		{
			rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
		}
	}
	return rtspServer;
}


// -----------------------------------------
//    create FramedSource server
// -----------------------------------------
FramedSource* createFramedSource(UsageEnvironment* env, int format, DeviceCapture* videoCapture, int outfd, int queueSize, bool useThread, bool repeatConfig, bool muxTS)
{
	FramedSource* source = NULL;
	if (format == V4L2_PIX_FMT_H264)
	{
		source = H264_V4L2DeviceSource::createNew(*env, videoCapture, outfd, queueSize, useThread, repeatConfig, muxTS);
		if (muxTS)
		{
			MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(*env);
			muxer->addNewVideoSource(source, 5);
			source = muxer;
		}
	}
	else
	{
		source = V4L2DeviceSource::createNew(*env, videoCapture, outfd, queueSize, useThread);
	}
	return source;
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
//    convert V4L2 pix format to RTP mime
// -----------------------------------------
std::string getRtpFormat(int format, bool muxTS)
{
	std::string rtpFormat;
	if (muxTS)
	{
		rtpFormat = "video/MP2T";
	}
	else
	{
		switch(format)
		{	
			case V4L2_PIX_FMT_H264 : rtpFormat = "video/H264"; break;
			case V4L2_PIX_FMT_JPEG : rtpFormat = "video/JPEG"; break;
#ifdef V4L2_PIX_FMT_VP8
			case V4L2_PIX_FMT_VP8  : rtpFormat = "video/VP8" ; break;
#endif
		}
	}
	
	return rtpFormat;
}

// -----------------------------------------
//    convert string format to fourcc 
// -----------------------------------------
int decodeFormat(const char* fmt)
{
	char fourcc[4];
	memset(&fourcc, 0, sizeof(fourcc));
	if (fmt != NULL)
	{
		strncpy(fourcc, fmt, 4);	
	}
	return v4l2_fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
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
	V4l2DeviceFactory::IoType ioTypeIn  = V4l2DeviceFactory::IOTYPE_MMAP;
	V4l2DeviceFactory::IoType ioTypeOut = V4l2DeviceFactory::IOTYPE_MMAP;
	std::string url = "unicast";
	std::string murl = "multicast";
	bool useThread = true;
	std::string maddr;
	bool repeatConfig = true;
	int timeout = 65;
	bool muxTS = false;
	unsigned int hlsSegment = 0;
	const char* realm = NULL;
	std::list<std::string> userPasswordList;
	std::string audioDevice;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:" "I:P:p:m:u:M:ct:TS::R:U:" "rwsf::F:W:H:A::" "h")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose    = 1; if (optarg && *optarg=='v') verbose++;  break;
			case 'Q':	queueSize  = atoi(optarg); break;
			case 'O':	outputFile = optarg; break;
			
			// RTSP/RTP
			case 'I':       ReceivingInterfaceAddr  = inet_addr(optarg); break;
			case 'P':	rtspPort                = atoi(optarg); break;
			case 'p':	rtspOverHTTPPort        = atoi(optarg); break;
			case 'u':	url                     = optarg; break;
			case 'm':	multicast = true; murl  = optarg; break;
			case 'M':	multicast = true; maddr = optarg; break;
			case 'c':	repeatConfig            = false; break;
			case 't':	timeout                 = atoi(optarg); break;
			case 'T':	muxTS                   = true; break;
			case 'S':	hlsSegment              = optarg ? atoi(optarg) : 5; muxTS=true; break;
			case 'R':       realm                   = optarg; break;
			case 'U':       userPasswordList.push_back(optarg); break;
			
			// V4L2
			case 'r':	ioTypeIn  = V4l2DeviceFactory::IOTYPE_READ; break;
			case 'w':	ioTypeOut = V4l2DeviceFactory::IOTYPE_READ; break;	
			case 's':	useThread =  false; break;
			case 'f':	format    = decodeFormat(optarg); break;
			case 'F':	fps       = atoi(optarg); break;
			case 'W':	width     = atoi(optarg); break;
			case 'H':	height    = atoi(optarg); break;
			
			// ALSA
			case 'A' :      audioDevice = optarg ? optarg : "default"; break;			

			case 'h':
			default:
			{
				std::cout << argv[0] << " [-v[v]] [-Q queueSize] [-O file]"                                        << std::endl;
				std::cout << "\t          [-I interface] [-P RTSP port] [-p RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] [-T] [-S[duration]]" << std::endl;
				std::cout << "\t          [-r] [-w] [-s] [-f[format] [-W width] [-H height] [-F fps] [device] [device]"         << std::endl;
				std::cout << "\t -v        : verbose"                                                               << std::endl;
				std::cout << "\t -vv       : very verbose"                                                          << std::endl;
				std::cout << "\t -Q length : Number of frame queue  (default "<< queueSize << ")"                   << std::endl;
				std::cout << "\t -O output : Copy captured frame to a file or a V4L2 device"                        << std::endl;
				
				std::cout << "\t RTSP/RTP options :"                                                                << std::endl;
				std::cout << "\t -I addr   : RTSP interface (default autodetect)"                                   << std::endl;
				std::cout << "\t -P port   : RTSP port (default "<< rtspPort << ")"                                 << std::endl;
				std::cout << "\t -p port   : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")"               << std::endl;
				std::cout << "\t -U user:password : RTSP user and password"                                         << std::endl;
				std::cout << "\t -R realm  : use md5 password 'md5(<username>:<realm>:<password>')"                 << std::endl;
				std::cout << "\t -u url    : unicast url (default " << url << ")"                                   << std::endl;
				std::cout << "\t -m url    : multicast url (default " << murl << ")"                                << std::endl;
				std::cout << "\t -M addr   : multicast group:port (default is random_address:20000)"                << std::endl;
				std::cout << "\t -c        : don't repeat config (default repeat config before IDR frame)"          << std::endl;
				std::cout << "\t -t timeout: RTCP expiration timeout in seconds (default " << timeout << ")"        << std::endl;
				std::cout << "\t -T        : send Transport Stream instead of elementary Stream"                    << std::endl;				
				std::cout << "\t -S[duration]: enable HLS & MPEG-DASH with segment duration  in seconds (default 5)"<< std::endl;
				
				std::cout << "\t V4L2 options :"                                                                    << std::endl;
				std::cout << "\t -r        : V4L2 capture using read interface (default use memory mapped buffers)" << std::endl;
				std::cout << "\t -w        : V4L2 capture using write interface (default use memory mapped buffers)"<< std::endl;
				std::cout << "\t -s        : V4L2 capture using live555 mainloop (default use a reader thread)"     << std::endl;
				std::cout << "\t -f        : V4L2 capture using current capture format (-W,-H,-F are ignored)"      << std::endl;
				std::cout << "\t -fformat  : V4L2 capture using format (-W,-H,-F are used)"                         << std::endl;
				std::cout << "\t -W width  : V4L2 capture width (default "<< width << ")"                           << std::endl;
				std::cout << "\t -H height : V4L2 capture height (default "<< height << ")"                         << std::endl;
				std::cout << "\t -F fps    : V4L2 capture framerate (default "<< fps << ")"                         << std::endl;
				std::cout << "\t device    : V4L2 capture device (default "<< dev_name << ")"                       << std::endl;
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
	struct in_addr destinationAddress;
	destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);
	unsigned short rtpPortNum = 20000;
	unsigned short rtcpPortNum = rtpPortNum+1;
	unsigned char ttl = 5;
	decodeMulticastUrl(maddr, destinationAddress, rtpPortNum, rtcpPortNum);	
	
	// create RTSP server
	RTSPServer* rtspServer = createRTSPServer(*env, rtspPort, rtspOverHTTPPort, timeout, hlsSegment, userPasswordList, realm);
	if (rtspServer == NULL) 
	{
		LOG(ERROR) << "Failed to create RTSP server: " << env->getResultMsg();
	}
	else
	{			
		V4l2Output* out = NULL;
		int nbSource = 0;
		std::list<std::string>::iterator devIt;
		for ( devIt=devList.begin() ; devIt!=devList.end() ; ++devIt)
		{
			std::string deviceName(*devIt);
			
			// Init capture
			LOG(NOTICE) << "Create V4L2 Source..." << deviceName;
			V4L2DeviceParameters param(deviceName.c_str(),format,width,height,fps, verbose);
			V4l2Capture* videoCapture = V4l2DeviceFactory::CreateVideoCapture(param, ioTypeIn);
			if (videoCapture)
			{
				nbSource++;
				format = videoCapture->getFormat();				
				int outfd = -1;
				
				if (!outputFile.empty())
				{
					V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0,verbose);
					out = V4l2DeviceFactory::CreateVideoOutput(outparam, ioTypeOut);
					if (out != NULL)
					{
						outfd = out->getFd();
					}
				}
				
				LOG(NOTICE) << "Create Source ..." << deviceName;
				std::string rtpFormat(getRtpFormat(format, muxTS));
				FramedSource* videoSource = createFramedSource(env, videoCapture->getFormat(), new V4L2DeviceCapture<V4l2Capture>(videoCapture), outfd, queueSize, useThread, repeatConfig, muxTS);
				if (videoSource == NULL) 
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
					
					StreamReplicator* replicator = StreamReplicator::createNew(*env, videoSource, false);
					
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
						addSession(rtspServer, baseUrl+murl, MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, replicator,rtpFormat));					
						
						// increment ports for next sessions
						rtpPortNum+=2;
						rtcpPortNum+=2;
						
					}
					// Create Unicast Session					
					if (hlsSegment > 0)
					{
						addSession(rtspServer, baseUrl+url, HLSServerMediaSubsession::createNew(*env,replicator,rtpFormat, hlsSegment));
						struct in_addr ip;
						ip.s_addr = ourIPAddress(*env);
						LOG(NOTICE) << "HLS       http://" << inet_ntoa(ip) << ":" << rtspPort << "/" << baseUrl+url << ".m3u8";
						LOG(NOTICE) << "MPEG-DASH http://" << inet_ntoa(ip) << ":" << rtspPort << "/" << baseUrl+url << ".mpd";
					}
					else
					{
						addSession(rtspServer, baseUrl+url, UnicastServerMediaSubsession::createNew(*env,replicator,rtpFormat));
					}
				}	
			}
		}
		if (!audioDevice.empty())
		{
			ALSACaptureParameters param(audioDevice.c_str(), 44100, 2, verbose);
			ALSACapture* audioCapture = ALSACapture::createNew(param);
			FramedSource* audioSource = V4L2DeviceSource::createNew(*env, new V4L2DeviceCapture<ALSACapture>(audioCapture), -1, queueSize, useThread);
			if (audioSource == NULL) 
			{
				LOG(FATAL) << "Unable to create source for device " << audioDevice;
				delete audioCapture;
			}
			else
			{
				nbSource++;
				StreamReplicator* audioReplicator = StreamReplicator::createNew(*env, audioSource, false);
				addSession(rtspServer, "audio", UnicastServerMediaSubsession::createNew(*env,audioReplicator,"audio/L16"));				
			}
		}		

		if (nbSource>0)
		{
			// main loop
			signal(SIGINT,sighandler);
			env->taskScheduler().doEventLoop(&quit); 
			LOG(NOTICE) << "Exiting....";			
		}
		
		Medium::close(rtspServer);

		if (out)
		{
			delete out;
		}
	}
	
	env->reclaim();
	delete scheduler;	
	
	return 0;
}



