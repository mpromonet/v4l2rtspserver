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


#include <sstream>

#include "RTSPServer.hh"
#include "RTSPCommon.hh"
#include <time.h>
#include "ByteStreamMemoryBufferSource.hh"
#include "TCPStreamSink.hh"

// -----------------------------------------
//  RTSP server supporting live HLS
// -----------------------------------------
class HLSServer : public RTSPServer
{

	class HLSClientConnection : public RTSPServer::RTSPClientConnection
	{
	public:
		HLSClientConnection(RTSPServer& ourServer, int clientSocket, struct sockaddr_in clientAddr)
		  : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr), fClientSessionId(0), fTCPSink(NULL) {
		}

		~HLSClientConnection() {
			if (fTCPSink != NULL)
			{
				fTCPSink->stopPlaying();
				Medium::close(fTCPSink);
			}
		}

	private:

		void sendHeader(const char* contentType, unsigned int contentLength)
		{
			// Construct our response:
			snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
			   "HTTP/1.1 200 OK\r\n"
			   "%s"
			   "Server: LIVE555 Streaming Media v%s\r\n"
			   "Content-Type: %s\r\n"
			   "Content-Length: %d\r\n"
			   "\r\n",
			   dateHeader(),
			   LIVEMEDIA_LIBRARY_VERSION_STRING,
			   contentType,
			   contentLength);

			  // Send the response header 
			  send(fClientOutputSocket, (char const*)fResponseBuffer, strlen((char*)fResponseBuffer), 0);
			  fResponseBuffer[0] = '\0'; // We've already sent the response.  This tells the calling code not to send it again.
		}
		
		void streamSource(FramedSource* source)
		{
		      if (fTCPSink != NULL) 
		      {
				fTCPSink->stopPlaying();
				Medium::close(fTCPSink);
				fTCPSink = NULL;
		      }
		      if (source != NULL) 
		      {
				fTCPSink = TCPStreamSink::createNew(envir(), fClientOutputSocket);
				fTCPSink->startPlaying(*source, afterStreaming, this);
		      }
		}
		
		void sendPlayList(char const* urlSuffix)
		{
			// First, make sure that the named file exists, and is streamable:
			ServerMediaSession* session = fOurServer.lookupServerMediaSession(urlSuffix);
			if (session == NULL) {
				handleHTTPCmd_notFound();
				return;
			}

			// To be able to construct a playlist for the requested file, we need to know its duration:
			float duration = session->duration();
			if (duration <= 0.0) {
				handleHTTPCmd_notSupported();
				return;
			}

			ServerMediaSubsessionIterator iter(*session);
			ServerMediaSubsession* subsession = iter.next();
			if (subsession == NULL) {
				handleHTTPCmd_notSupported();
				return;			  
			}

			unsigned int startTime = subsession->getCurrentNPT(NULL);
			unsigned sliceDuration = 10;		  
			std::ostringstream os;
			os  	<< "#EXTM3U\r\n"
				<< "#EXT-X-ALLOW-CACHE:YES\r\n"
				<< "#EXT-X-MEDIA-SEQUENCE:" << startTime <<  "\r\n"
				<< "#EXT-X-TARGETDURATION:" << sliceDuration << "\r\n";

			for (unsigned int slice=0; slice*sliceDuration<duration; slice++)
			{
				os << "#EXTINF:" << sliceDuration << ",\r\n";
				os << urlSuffix << "?segment=" << (startTime+slice*sliceDuration) << "\r\n";
			}

			const std::string& playList(os.str());

			// send response header
			this->sendHeader("application/vnd.apple.mpegurl", playList.size());
			
			// stream body
			u_int8_t* playListBuffer = new u_int8_t[playList.size()];
			memcpy(playListBuffer, playList.c_str(), playList.size());
			this->streamSource(ByteStreamMemoryBufferSource::createNew(envir(), playListBuffer, playList.size()));
		}
		
		void handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* /*fullRequestStr*/) {
		  // If "urlSuffix" ends with "?segment=<offset-in-seconds>,<duration-in-seconds>", then strip this off, and send the
		  // specified segment.  Otherwise, construct and send a playlist that consists of segments from the specified file.
		  do {
		    char const* questionMarkPos = strrchr(urlSuffix, '?');
		    if (questionMarkPos == NULL) break;
		    unsigned offsetInSeconds;
		    if (sscanf(questionMarkPos, "?segment=%u", &offsetInSeconds) != 1) break;

		    char* streamName = strDup(urlSuffix);
		    streamName[questionMarkPos-urlSuffix] = '\0';

		    do {
		      ServerMediaSession* session = fOurServer.lookupServerMediaSession(streamName);
		      if (session == NULL) {
			handleHTTPCmd_notFound();
			break;
		      }

		      // We can't send multi-subsession streams over HTTP (because there's no defined way to multiplex more than one subsession).
		      // Therefore, use the first (and presumed only) substream:
		      ServerMediaSubsessionIterator iter(*session);
		      ServerMediaSubsession* subsession = iter.next();
		      if (subsession == NULL) {
			// Treat an 'empty' ServerMediaSession the same as one that doesn't exist at all:
			handleHTTPCmd_notFound();
			break;
		      }

		      // Call "getStreamParameters()" to create the stream's source.  (Because we're not actually streaming via RTP/RTCP, most
		      // of the parameters to the call are dummy.)
		      ++fClientSessionId;
		      Port clientRTPPort(0), clientRTCPPort(0), serverRTPPort(0), serverRTCPPort(0);
		      netAddressBits destinationAddress = 0;
		      u_int8_t destinationTTL = 0;
		      Boolean isMulticast = False;
		      void* streamToken = NULL;
		      subsession->getStreamParameters(fClientSessionId, 0, clientRTPPort,clientRTCPPort, -1,0,0, destinationAddress,destinationTTL, isMulticast, serverRTPPort,serverRTCPPort, streamToken);
		      
		      // Seek the stream source to the desired place, with the desired duration, and (as a side effect) get the number of bytes:
		      double dOffsetInSeconds = (double)offsetInSeconds;
		      u_int64_t numBytes = 0;
		      subsession->seekStream(fClientSessionId, streamToken, dOffsetInSeconds, 0.0, numBytes);
		      
		      if (numBytes == 0) {
			// For some reason, we do not know the size of the requested range.  We can't handle this request:
			handleHTTPCmd_notSupported();
			break;
		      }
		      
		      // send response header
		      this->sendHeader("video/mp2t", numBytes);
		      
		      // stream body
		      this->streamSource(subsession->getStreamSource(streamToken));

		    } while(0);

		    delete[] streamName;
		    return;
		  } while (0);

		  this->sendPlayList(urlSuffix);
		}

		static void afterStreaming(void* clientData) 
		{
			HLSServer::HLSClientConnection* clientConnection = (HLSServer::HLSClientConnection*)clientData;
			// Arrange to delete the 'client connection' object:
			if (clientConnection->fRecursionCount > 0) {
				// We're still in the midst of handling a request
				clientConnection->fIsActive = False; // will cause the object to get deleted at the end of handling the request
			} else {
				// We're no longer handling a request; delete the object now:
				delete clientConnection;
			}
		}
		private:
		    u_int32_t fClientSessionId;
		    TCPStreamSink* fTCPSink;
	};
	
	public:
		static HLSServer* createNew(UsageEnvironment& env, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds) {
			int ourSocket = setUpOurSocket(env, rtspPort);
			if (ourSocket == -1) return NULL;
			return new HLSServer(env, ourSocket, rtspPort, authDatabase, reclamationTestSeconds);
		}

		HLSServer(UsageEnvironment& env, int ourSocket, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
		  : RTSPServer(env, ourSocket, rtspPort, authDatabase, reclamationTestSeconds) {
		}

		RTSPServer::RTSPClientConnection* createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr) {
			return new HLSClientConnection(*this, clientSocket, clientAddr);
		}
};

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
//    create RTSP server
// -----------------------------------------
RTSPServer* createRTSPServer(UsageEnvironment& env, unsigned short rtspPort, unsigned short rtspOverHTTPPort, int timeout)
{
	UserAuthenticationDatabase* authDB = NULL;	
	RTSPServer* rtspServer = HLSServer::createNew(env, rtspPort, authDB, timeout);
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

std::string getRtpFormat(int format)
{
	std::string rtpFormat;
	switch(format)
	{	
		case V4L2_PIX_FMT_H264 : rtpFormat = "video/H264"; break;
#ifdef V4L2_PIX_FMT_VP8
		case V4L2_PIX_FMT_VP8  : rtpFormat = "video/VP8" ; break;
#endif
	}
	return rtpFormat;
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
	bool muxTS = false;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:" "I:P:p:m:u:M:ct:T" "rsfF:W:H:" "h")) != -1)
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
			// V4L2
			case 'r':	useMmap   =  false; break;
			case 's':	useThread =  false; break;
			case 'f':	format    = 0; break;
			case 'F':	fps       = atoi(optarg); break;
			case 'W':	width     = atoi(optarg); break;
			case 'H':	height    = atoi(optarg); break;

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
				std::cout << "\t RTSP/RTP options :"                                                               << std::endl;
				std::cout << "\t -I addr  : RTSP interface (default autodetect)"                                   << std::endl;
				std::cout << "\t -P port  : RTSP port (default "<< rtspPort << ")"                                 << std::endl;
				std::cout << "\t -p port  : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")"               << std::endl;
				std::cout << "\t -u url   : unicast url (default " << url << ")"                                   << std::endl;
				std::cout << "\t -m url   : multicast url (default " << murl << ")"                                << std::endl;
				std::cout << "\t -M addr  : multicast group:port (default is random_address:20000)"                << std::endl;
				std::cout << "\t -c       : don't repeat config (default repeat config before IDR frame)"          << std::endl;
				std::cout << "\t -t secs  : RTCP expiration timeout (default " << timeout << ")"                   << std::endl;
				std::cout << "\t -T       : send Transport Stream instead of elementary Stream"                    << std::endl;
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
	RTSPServer* rtspServer = createRTSPServer(*env, rtspPort, rtspOverHTTPPort, timeout);
	if (rtspServer == NULL) 
	{
		LOG(ERROR) << "Failed to create RTSP server: " << env->getResultMsg();
	}
	else
	{			
		int nbSource = 0;
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
				nbSource++;
				format = videoCapture->getFormat();				
				int outfd = -1;
				
				V4l2Output* out = NULL;
				if (!outputFile.empty())
				{
					V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0,verbose);
					V4l2Output* out = V4l2DeviceFactory::CreateVideoOutput(outparam, useMmap);
					if (out != NULL)
					{
						outfd = out->getFd();
					}
				}
				
				LOG(NOTICE) << "Start V4L2 Capture..." << deviceName;
				if (!videoCapture->captureStart())
				{
					LOG(NOTICE) << "Cannot start V4L2 Capture for:" << deviceName;
				}
				FramedSource* videoES = NULL;
				std::string rtpFormat(getRtpFormat(format));
				if (format == V4L2_PIX_FMT_H264)
				{
					videoES = H264_V4L2DeviceSource::createNew(*env, param, videoCapture, outfd, queueSize, useThread, repeatConfig, muxTS);
					if (muxTS)
					{
						MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(*env);
						muxer->addNewVideoSource(videoES, 5);
						videoES = muxer;
						rtpFormat = "video/MP2T";
					}
				}
				else
				{
					videoES = V4L2DeviceSource::createNew(*env, param, videoCapture, outfd, queueSize, useThread);
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
						addSession(rtspServer, baseUrl+murl, MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, replicator,rtpFormat));					
						
						// increment ports for next sessions
						rtpPortNum+=2;
						rtcpPortNum+=2;
						
					}
					// Create Unicast Session					
					if (muxTS)
					{
						addSession(rtspServer, baseUrl+url, HLSServerMediaSubsession::createNew(*env,replicator,rtpFormat));
					}
					else
					{
						addSession(rtspServer, baseUrl+url, UnicastServerMediaSubsession::createNew(*env,replicator,rtpFormat));
					}
				}	
				if (out)
				{
					delete out;
				}
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
	}
	
	env->reclaim();
	delete scheduler;	
	
	return 0;
}



