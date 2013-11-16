/***************************************************************************************/
/* V4L2 RTSP streamer                                                                  */
/*                                                                                     */
/* H264 capture using V4L2                                                             */  
/* RTSP using live555                                                                  */  
/*                                                                                     */
/* NOTE : Configuration SPS/PPS need to be captured in one single frame                */
/***************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <linux/videodev2.h>

#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <list>

// libv4l2
#include <libv4l2.h>

// live555
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <Base64.hh>

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
const char marker[] = {0,0,0,1};
class V4L2DeviceSource: public FramedSource 
{
	public:
		// ---------------------------------
		// V4L2 Capture parameters
		// ---------------------------------
		struct V4L2DeviceParameters 
		{
			V4L2DeviceParameters(const char* devname, int format, int queueSize, int width, int height, int fps, bool verbose,const std::string & outputFile) : 
				m_devName(devname), m_format(format), m_queueSize(queueSize), m_width(width), m_height(height), m_fps(fps), m_verbose(verbose), m_outputFIle(outputFile) {};
				
			std::string m_devName;
			int m_width;
			int m_height;
			int m_format;
			int m_queueSize;
			int m_fps;			
			bool m_verbose;
			std::string m_outputFIle;
		};

		// ---------------------------------
		// Captured frame
		// ---------------------------------
		struct Frame
		{
			Frame(char* buffer, int size, timeval timestamp) : m_buffer(buffer), m_size(size), m_timestamp(timestamp) { };
			~Frame()  { delete m_buffer; };
			
			char* m_buffer;
			int m_size;
			timeval m_timestamp;
		};
		
		// ---------------------------------
		// compute FPS
		// ---------------------------------
		class Fps
		{
			public:
				Fps(const std::string & msg) : m_fps(0), m_fps_sec(0), m_msg(msg) {};
				
			public:
				int notify(int tv_sec)
				{
					m_fps++;
					if (tv_sec != m_fps_sec)
					{
						std::cout << m_msg  << "tv_sec:" <<   tv_sec << " fps:" << m_fps <<"\n";		
						m_fps_sec = tv_sec;
						m_fps = 0;
					}
					return m_fps;
				}
			
			protected:
				int m_fps;
				int m_fps_sec;
				const std::string m_msg;
		};
		
	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params) 
		{ 
			V4L2DeviceSource* device = new V4L2DeviceSource(env, params); 
			if (device && !device->init())
			{
				delete device;
				device=NULL;
			}
			return device;
		}
		int getBufferSize() { return m_bufferSize; };
		std::string getAuxLine() { return m_auxLine; };

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : FramedSource(env), m_params(params), m_fd(-1), m_bufferSize(0), m_in("in"), m_out("out") , m_outfile(NULL)
		{
			m_eventTriggerId = envir().taskScheduler().createEventTrigger(V4L2DeviceSource::deliverFrameStub);
		}
		
		virtual ~V4L2DeviceSource()
		{
			envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
			v4l2_close(m_fd);
			if (m_outfile) fclose(m_outfile);
		}

	protected:
		bool init()
		{
			m_fd = initdevice(m_params.m_devName.c_str());
			if (m_fd == -1)
			{
				fprintf(stderr, "Init device:%s failure\n", m_params.m_devName.c_str());
			}
			else
			{
				envir().taskScheduler().turnOnBackgroundReadHandling( m_fd, V4L2DeviceSource::incomingPacketHandlerStub,this);
			}
			return (m_fd!=-1);
		}
		
		int xioctl(int fd, int request, void *arg)
		{
			int r = -1;

			do 
			{
				r = v4l2_ioctl(fd, request, arg);
			} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

			return r;
		}
		
		int checkCapabilities(int fd)
		{
			struct v4l2_capability cap;
			memset(&(cap), 0, sizeof(cap));
			if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) 
			{
				fprintf(stderr, "xioctl cannot get capabilities error %d, %s\n", errno, strerror(errno));
				return -1;
			}
			fprintf(stderr, "driver:%s capabilities;%X\n", cap.driver, cap.capabilities);

			if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
			{
				fprintf(stderr, "%s is no video capture device\n", m_params.m_devName.c_str());
				return -1;
			}
			
			if ((cap.capabilities & V4L2_CAP_READWRITE)) fprintf(stderr, "%s support read i/o\n", m_params.m_devName.c_str());
			if ((cap.capabilities & V4L2_CAP_STREAMING))  fprintf(stderr, "%s support streaming i/o\n", m_params.m_devName.c_str());
			if ((cap.capabilities & V4L2_CAP_TIMEPERFRAME)) fprintf(stderr, "%s support timeperframe\n", m_params.m_devName.c_str());
			
			return 0;
		}

		int configureFormat(int fd)
		{
			struct v4l2_format   fmt;			
			memset(&(fmt), 0, sizeof(fmt));
			fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			fmt.fmt.pix.width       = m_params.m_width;
			fmt.fmt.pix.height      = m_params.m_height;
			fmt.fmt.pix.pixelformat = m_params.m_format;
			fmt.fmt.pix.field       = V4L2_FIELD_ANY;
			
			if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
			{
				fprintf(stderr, "xioctl cannot set format error %d, %s\n", errno, strerror(errno));
				return -1;
			}			
			if (fmt.fmt.pix.pixelformat != m_params.m_format) 
			{
				printf("Libv4l didn't accept format (%d). Can't proceed.\n", m_params.m_format);
				return -1;
			}
			
			if ((fmt.fmt.pix.width != m_params.m_width) || (fmt.fmt.pix.height != m_params.m_height))
			{
				printf("Warning: driver is sending image at %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.width);
			}
			
			m_bufferSize =  fmt.fmt.pix.sizeimage;
			return 0;
		}

		int configureParam(int fd)
		{
			struct v4l2_streamparm   param;			
			memset(&(param), 0, sizeof(param));
			param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			param.parm.capture.timeperframe.numerator = 1;
			param.parm.capture.timeperframe.denominator = m_params.m_fps;

			if (xioctl(fd, VIDIOC_S_PARM, &param) == -1)
			{
				fprintf(stderr, "xioctl cannot set param error %d, %s\n", errno, strerror(errno));
				return -1;
			}
			
			fprintf(stderr, "fps :%d/%d nbBuffer:%d\n", param.parm.capture.timeperframe.numerator, param.parm.capture.timeperframe.denominator, param.parm.capture.readbuffers);
			
			return 0;
		}
		
		int initdevice(const char *dev_name)
		{
			int fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
			if (fd < 0) 
			{
				perror("Cannot open device");
				return -1;
			}
			
			if (checkCapabilities(fd) !=0)
			{
				return -1;
			}
			
			if (configureFormat(fd) !=0)
			{
				return -1;
			}

			if (configureParam(fd) !=0)
			{
				return -1;
			}
			if (!m_params.m_outputFIle.empty())
			{
				fprintf(stderr, "OutputFile:%s\n", m_params.m_outputFIle.c_str());
				m_outfile = fopen(m_params.m_outputFIle.c_str(),"w");
			}
			
			return fd;
		}
		
		virtual void doGetNextFrame()
		{
			if (!m_captureQueue.empty())
			{
				deliverFrame();
			}
		}
				
		static void deliverFrameStub(void* clientData)
		{
			((V4L2DeviceSource*) clientData)->deliverFrame();
		}	
		
		virtual void deliverFrame()
		{			
			if (isCurrentlyAwaitingData()) 
			{
				fDurationInMicroseconds = 0;
				fFrameSize = 0;
				
				if (m_captureQueue.empty())
				{
					if ( m_params.m_verbose) envir() << "Queue is empty \n";		
				}
				else
				{				
					gettimeofday(&fPresentationTime, NULL);			
					m_out.notify(fPresentationTime.tv_sec);
					
					Frame * frame = m_captureQueue.front();
					m_captureQueue.pop_front();
													
					if (frame->m_size > fMaxSize) 
					{
						fFrameSize = fMaxSize;
						fNumTruncatedBytes = frame->m_size - fMaxSize;
					} 
					else 
					{
						fFrameSize = frame->m_size;
					}
					timeval diff;
					timersub(&fPresentationTime,&(frame->m_timestamp),&diff);
					
					if (m_params.m_verbose) 
					{
						printf ("deliverFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d\n",fPresentationTime.tv_sec, fPresentationTime.tv_usec, fFrameSize,  (int)(diff.tv_sec*1000+diff.tv_usec/1000),  m_captureQueue.size());
					}
					
					memcpy(fTo, frame->m_buffer, fFrameSize);
					delete frame;
				}
				FramedSource::afterGetting(this);			
			}
		}
		
		static void incomingPacketHandlerStub(void* clientData, int mask)
		{
			((V4L2DeviceSource*) clientData)->getNextFrame();
		}	
		
		void getNextFrame() 
		{
			char* buffer = new char[m_bufferSize];
			
			timeval ref;
			gettimeofday(&ref, NULL);											
			int frameSize = v4l2_read(m_fd, buffer,  m_bufferSize);
			
			if (frameSize < 0)
			{
				envir() << "V4L2DeviceSource::getNextFrame fd:"  << m_fd << " errno:" << errno << " "  << strerror(errno) << "\n";		
				delete buffer;
				handleClosure(this);
			}
			else
			{
				timeval tv;
				gettimeofday(&tv, NULL);												
				timeval diff;
				timersub(&tv,&ref,&diff);
				int fps = m_in.notify(tv.tv_sec);
				if (m_params.m_verbose) 
				{
					printf ("getNextFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d\n", ref.tv_sec, ref.tv_usec, frameSize, (int)(diff.tv_sec*1000+diff.tv_usec/1000), m_captureQueue.size());
				}
				processFrame(buffer,frameSize,ref);
				if (!processConfigrationFrame(buffer,frameSize))
				{
					queueFrame(buffer,frameSize,ref);
				}
			}			
		}	

		bool processConfigrationFrame(char * frame, int frameSize) 
		{
			bool ret = false;
			
			// save SPS and PPS
			u_int8_t nal_unit_type = frame[0]&0x1F;				
			if (nal_unit_type == 7)
			{
				std::cout << "SPS\n";	
				for (int i=0; i < frameSize; ++i)
				{
					if (memcmp(&frame[i],marker,sizeof(marker)) == 0)
					{
						size_t spsSize = i ;
						std::cout << "PPS" << "\n";	
						char* sps = (char*)memcpy(new char [spsSize], frame, spsSize);
						size_t ppsSize = frameSize - spsSize - sizeof(marker);
						char* pps = (char*)memcpy(new char [ppsSize], &frame[spsSize+sizeof(marker)], ppsSize);
												
						u_int32_t profile_level_id = 0;
						if (spsSize >= 4) 
						{
							profile_level_id = (sps[1]<<16)|(sps[2]<<8)|sps[3]; 
						}
						
						char* sps_base64 = base64Encode(sps, spsSize);
						char* pps_base64 = base64Encode(pps, ppsSize);		

						std::ostringstream os; 
						os << "a=fmtp:96 packetization-mode=1";
						os << ";profile-level-id=" << std::hex << std::setw(6) << profile_level_id;
						os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
						os << "\r\n";
						m_auxLine.assign(os.str());
						
						free(sps);
						free(pps);
						free(sps_base64);
						free(pps_base64);
						
						std::cout << "AuxLine:"  << m_auxLine << " \n";		
						ret = true;
						break;
					}						
				}
				delete [] frame;
			}
			return ret;
		}
		
		void processFrame(char * frame, int &frameSize, const timeval &ref) 
		{
			timeval tv;
			gettimeofday(&tv, NULL);												
			timeval diff;
			timersub(&tv,&ref,&diff);
			
			if (m_params.m_verbose) 
			{
				printf ("queueFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d data:%02X%02X%02X%02X%02X...\n", ref.tv_sec, ref.tv_usec, frameSize, (int)(diff.tv_sec*1000+diff.tv_usec/1000), m_captureQueue.size(), frame[0], frame[1], frame[2], frame[3], frame[4]);
			}
			if (m_outfile) fwrite(frame, frameSize,1, m_outfile);

			// remove marker
			if (memcmp(frame,marker,sizeof(marker)) == 0)
			{
				frameSize -= sizeof(marker);
				memmove(frame, &frame[sizeof(marker)], frameSize);
			}
		}	
		
		void queueFrame(char * frame, int frameSize, const timeval &tv) 
		{
			while (m_captureQueue.size() >= m_params.m_queueSize)
			{
				if (m_params.m_verbose) 
				{
					envir() << "Queue full size drop frame size:"  << (int)m_captureQueue.size() << " \n";		
				}
				delete m_captureQueue.front();
				m_captureQueue.pop_front();
			}
			m_captureQueue.push_back(new Frame(frame, frameSize,tv));	  				
			envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
		}	


	private:
		V4L2DeviceParameters m_params;
		int m_fd;
		int m_bufferSize;
		std::list<Frame*> m_captureQueue;
		Fps m_in;
		Fps m_out;
		EventTriggerId m_eventTriggerId;
		FILE* m_outfile;
		std::string m_auxLine;
};

// ---------------------------------
//   BaseServerMediaSubsession
// ---------------------------------
class BaseServerMediaSubsession
{
	public:
		static FramedSource* createSource(UsageEnvironment& env, FramedSource * videoES, int format)
		{
			FramedSource* source = NULL;
			switch (format)
			{
				case V4L2_PIX_FMT_H264 : source = H264VideoStreamDiscreteFramer::createNew(env, videoES); break;
			}
			return source;
		}

		static RTPSink* createSink(UsageEnvironment& env, Groupsock * rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, int format)
		{
			RTPSink* videoSink = NULL;
			switch (format)
			{
				case V4L2_PIX_FMT_H264 : videoSink = H264VideoRTPSink::createNew(env, rtpGroupsock,rtpPayloadTypeIfDynamic); break;
			}
			return videoSink;
		}

		char const* getAuxLine(V4L2DeviceSource* source,unsigned char rtpPayloadType)
		{
			const char* auxLine = NULL;
			if (source)
			{
				auxLine = strdup(source->getAuxLine().c_str());
			} 
			return auxLine;
		}
};

// -----------------------------------------
//    ServerMediaSubsession for Multicast
// -----------------------------------------
class MulticastServerMediaSubsession : public PassiveServerMediaSubsession , public BaseServerMediaSubsession
{
	public:
		static MulticastServerMediaSubsession* createNew(UsageEnvironment& env
												, struct in_addr destinationAddress
												, Port rtpPortNum, Port rtcpPortNum
												, int ttl
												, unsigned char rtpPayloadType
												, StreamReplicator* replicator
												, int format) 
		{ 
			// Create a source
			FramedSource* source = replicator->createStreamReplica();			
			FramedSource* videoSource = createSource(env, source, format);

			// Create RTP/RTCP groupsock
			Groupsock* rtpGroupsock = new Groupsock(env, destinationAddress, rtpPortNum, ttl);
			Groupsock* rtcpGroupsock = new Groupsock(env, destinationAddress, rtcpPortNum, ttl);

			// Create a RTP sink
			RTPSink* videoSink = createSink(env,rtpGroupsock, rtpPayloadType, format);

			// Create 'RTCP instance'
			const unsigned maxCNAMElen = 100;
			unsigned char CNAME[maxCNAMElen+1];
			gethostname((char*)CNAME, maxCNAMElen);
			CNAME[maxCNAMElen] = '\0'; 
			RTCPInstance* rtcpInstance = RTCPInstance::createNew(env, rtcpGroupsock,  500, CNAME, videoSink, NULL);

			// Start Playing the Sink
			videoSink->startPlaying(*videoSource, NULL, NULL);
			
			return new MulticastServerMediaSubsession(replicator, *videoSink,rtcpInstance);
		}
		
	protected:
		MulticastServerMediaSubsession(StreamReplicator* replicator, RTPSink& rtpSink, RTCPInstance* rtcpInstance) 
				: PassiveServerMediaSubsession(rtpSink, rtcpInstance), m_replicator(replicator), m_rtpSink(rtpSink) {};			

		virtual char const* sdpLines() 
		{
			if (m_SDPLines.empty())
			{
				// Ugly workaround to give SPS/PPS that are get from the RTPSink 
				m_SDPLines.assign(PassiveServerMediaSubsession::sdpLines());
				m_SDPLines.append(getAuxSDPLine(NULL,NULL));
			}
			return m_SDPLines.c_str();
		}

		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource)
		{
			const char* auxLine = NULL;
			V4L2DeviceSource* source = dynamic_cast<V4L2DeviceSource*>(m_replicator->inputSource());
			if (source)
			{
				auxLine = getAuxLine(source, m_rtpSink.rtpPayloadType());
			} 
			return auxLine;
		}
		
	protected:
		StreamReplicator* m_replicator;
		RTPSink& m_rtpSink;
		std::string m_SDPLines;
};

// -----------------------------------------
//    ServerMediaSubsession for Unicast
// -----------------------------------------
class UnicastServerMediaSubsession : public OnDemandServerMediaSubsession , public BaseServerMediaSubsession
{
	public:
		static UnicastServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, int format) 
		{ 
			return new UnicastServerMediaSubsession(env,replicator,format);
		}
		
	protected:
		UnicastServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, int format) 
				: OnDemandServerMediaSubsession(env, False), m_replicator(replicator), m_format(format) {};
			
		virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
		{
			FramedSource* source = m_replicator->createStreamReplica();
			return createSource(envir(), source, m_format);
		}
		
		virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
		{
			return createSink(envir(), rtpGroupsock,rtpPayloadTypeIfDynamic, m_format);
		}
		
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource)
		{
			const char* auxLine = NULL;
			V4L2DeviceSource* source = dynamic_cast<V4L2DeviceSource*>(m_replicator->inputSource());
			if (source)
			{
				auxLine = getAuxLine(source, rtpSink->rtpPayloadType());
			} 
			return auxLine;
		}
	protected:
		StreamReplicator* m_replicator;
		int m_format;
};

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
	unsigned short rtspOverHTTPPort = 8080;
	bool multicast = false;
	bool verbose = false;
	std::string outputFile;

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "hW:H:Q:P:F:vO:mT:")) != -1)
	{
		switch (c)
		{
			case 'O':	outputFile = optarg; break;
			case 'v':	verbose = true; break;
			case 'm':	multicast = true; break;
			case 'W':	width = atoi(optarg); break;
			case 'H':	height = atoi(optarg); break;
			case 'Q':	queueSize = atoi(optarg); break;
			case 'P':	rtspPort = atoi(optarg); break;
			case 'T':	rtspOverHTTPPort = atoi(optarg); break;
			case 'F':	fps = atoi(optarg); break;
			case 'h':
			{
				std::cout << argv[0] << " [-v][-m][-P RTSP port][-P RTSP/HTTP port][-Q queueSize] [-W width] [-H height] [-F fps] [-O file] [device]" << std::endl;
				std::cout << "\t -v       : Verbose " << std::endl;
				std::cout << "\t -Q length: Number of frame queue  (default "<< queueSize << ")" << std::endl;
				std::cout << "\t -O file  : Dump capture to a file" << std::endl;
				std::cout << "\t -m       : Enable multicast output" << std::endl;
				std::cout << "\t -P port  : RTSP port (default "<< rtspPort << ")" << std::endl;
				std::cout << "\t -H port  : RTSP over HTTP port (default "<< rtspOverHTTPPort << ")" << std::endl;
				std::cout << "\t -F fps   : V4L2 capture framerate (default "<< fps << ")" << std::endl;
				std::cout << "\t -W width : V4L2 capture width (default "<< width << ")" << std::endl;
				std::cout << "\t -H height: V4L2 capture height (default "<< height << ")" << std::endl;
				std::cout << "\t device   : V4L2 capture device (default "<< dev_name << ")" << std::endl;
				exit(0);
			}
		}
	}
	if (optind<argc)
	{
		dev_name = argv[optind];
	}
     
	// 
	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);	
	
	RTSPServer* rtspServer = RTSPServer::createNew(*env, rtspPort);
	if (rtspServer == NULL) 
	{
		*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	}
	else
	{
		// set http tunneling
		rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
		
		// Init capture
		*env << "Create V4L2 Source..." << dev_name << "\n";
		V4L2DeviceSource::V4L2DeviceParameters param(dev_name,format,queueSize,width,height,fps,verbose,outputFile);
		V4L2DeviceSource* videoES = V4L2DeviceSource::createNew(*env, param);
		if (videoES == NULL) 
		{
			*env << "Unable to create source \n";
		}
		else
		{
			destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*env);	
			OutPacketBuffer::maxSize = videoES->getBufferSize();
			StreamReplicator* replicator = StreamReplicator::createNew(*env, videoES, false);

			// Create Server Multicast Session
			if (multicast)
			{
				ServerMediaSession* sms = ServerMediaSession::createNew(*env, "multicast");
				sms->addSubsession(MulticastServerMediaSubsession::createNew(*env,destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, 96, replicator,format));
				rtspServer->addServerMediaSession(sms);

				char* url = rtspServer->rtspURL(sms);
				*env << "Play this stream using the URL \"" << url << "\"\n";
				delete[] url;			
			}
			
			// Create Server Unicast Session
			{
				ServerMediaSession* sms = ServerMediaSession::createNew(*env, "unicast");
				sms->addSubsession(UnicastServerMediaSubsession::createNew(*env,replicator,format));
				rtspServer->addServerMediaSession(sms);

				char* url = rtspServer->rtspURL(sms);
				*env << "Play this stream using the URL \"" << url << "\"\n";
				delete[] url;			
			}

			// main loop
			signal(SIGINT,sighandler);
			env->taskScheduler().doEventLoop(&quit); 
			*env << "Exiting..\n";			
		}
		
		Medium::close(videoES);
		Medium::close(rtspServer);
	}
	
	env->reclaim();
	delete scheduler;
	
	
	return 0;
}

