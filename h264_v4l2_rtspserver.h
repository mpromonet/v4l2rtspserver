/***************************************************************************************/
/* V4L2 RTSP streamer                                                                  */
/*                                                                                     */
/* H264 capture using V4L2                                                             */  
/* RTSP using live555                                                                  */  
/*                                                                                     */
/* NOTE : Configuration SPS/PPS need to be captured in one single frame                */
/***************************************************************************************/

#ifndef H264_V4L2_RTSP_SERVER
#define H264_V4L2_RTSP_SERVER

#include <string>
#include <list> 
#include <iostream>


// live555
#include <liveMedia.hh>

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
			Frame(char* buffer, int size, timeval timestamp) : m_buffer(buffer), m_size(size), m_timestamp(timestamp) {};
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
		static V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params);
		int getBufferSize() { return m_bufferSize; };
		std::string getAuxLine() { return m_auxLine; };

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params);
		virtual ~V4L2DeviceSource();

	protected:
		bool init();
		int initdevice(const char *dev_name);
		int checkCapabilities(int fd);
		int configureFormat(int fd);
		int configureParam(int fd);		
		int xioctl(int fd, int request, void *arg);
	
		virtual void doGetNextFrame();				
		static void deliverFrameStub(void* clientData) {((V4L2DeviceSource*) clientData)->deliverFrame();};
		void deliverFrame();
		static void incomingPacketHandlerStub(void* clientData, int mask) { ((V4L2DeviceSource*) clientData)->getNextFrame(); };
		void getNextFrame();
		bool processConfigrationFrame(char * frame, int frameSize);
		void processFrame(char * frame, int &frameSize, const timeval &ref);
		void queueFrame(char * frame, int frameSize, const timeval &tv);
	
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
		BaseServerMediaSubsession(StreamReplicator* replicator): m_replicator(replicator) {};
	
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
				std::ostringstream os; 
				os << "a=fmtp:" << int(rtpPayloadType) << " ";				
				os << source->getAuxLine();				
				os << "\r\n";				
				auxLine = strdup(os.str().c_str());
			} 
			return auxLine;
		}
		
	protected:
		StreamReplicator* m_replicator;
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
								, int format);
		
	protected:
		MulticastServerMediaSubsession(StreamReplicator* replicator, RTPSink* rtpSink, RTCPInstance* rtcpInstance) 
				: PassiveServerMediaSubsession(*rtpSink, rtcpInstance), BaseServerMediaSubsession(replicator), m_rtpSink(rtpSink) {};			

		virtual char const* sdpLines() ;
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource);
		
	protected:
		RTPSink* m_rtpSink;
		std::string m_SDPLines;
};

// -----------------------------------------
//    ServerMediaSubsession for Unicast
// -----------------------------------------
class UnicastServerMediaSubsession : public OnDemandServerMediaSubsession , public BaseServerMediaSubsession
{
	public:
		static UnicastServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, int format);
		
	protected:
		UnicastServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, int format) 
				: OnDemandServerMediaSubsession(env, False), BaseServerMediaSubsession(replicator), m_format(format) {};
			
		virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate);
		virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource);		
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource);
					
	protected:
		int m_format;
};

#endif