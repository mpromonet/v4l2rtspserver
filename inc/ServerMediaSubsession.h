/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#ifndef SERVER_MEDIA_SUBSESSION
#define SERVER_MEDIA_SUBSESSION

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sys/stat.h>

// live555
#include <liveMedia.hh>

// forward declaration
class V4L2DeviceSource;

// ---------------------------------
//   BaseServerMediaSubsession
// ---------------------------------
class BaseServerMediaSubsession
{
	public:
		BaseServerMediaSubsession(StreamReplicator* replicator): m_replicator(replicator) {};
	
	public:
		static FramedSource* createSource(UsageEnvironment& env, FramedSource * videoES, const std::string& format);
		static RTPSink* createSink(UsageEnvironment& env, Groupsock * rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, const std::string& format);
		char const* getAuxLine(V4L2DeviceSource* source,unsigned char rtpPayloadType);
		
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
								, StreamReplicator* replicator
								, const std::string& format);
		
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
		static UnicastServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format);
		
	protected:
		UnicastServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format) 
				: OnDemandServerMediaSubsession(env, False), BaseServerMediaSubsession(replicator), m_format(format) {};
			
		virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate);
		virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource);		
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource);	
					
	protected:
		const std::string m_format;
};

// -----------------------------------------
//    ServerMediaSubsession for HLS
// -----------------------------------------

class HLSSink : public MediaSink
{
	public:
		static HLSSink* createNew(UsageEnvironment& env, unsigned int bufferSize) 
		{
			return new HLSSink(env, bufferSize);
		}
		
	protected:
		HLSSink(UsageEnvironment& env, unsigned bufferSize) : MediaSink(env), m_bufferSize(bufferSize), m_slice(0), m_firstslice(0)
		{
			m_buffer = new unsigned char[m_bufferSize];
		}

		virtual ~HLSSink() 
		{
			delete[] m_buffer;
		}

		
		virtual Boolean continuePlaying() 
		{
			Boolean ret = False;
			if (fSource != NULL) 
			{
				fSource->getNextFrame(m_buffer, m_bufferSize,
						afterGettingFrame, this,
						onSourceClosure, this);
				ret = True;
			}
			return ret;
		}

		static void afterGettingFrame(void* clientData, unsigned frameSize,
						 unsigned numTruncatedBytes,
						 struct timeval presentationTime,
						 unsigned /*durationInMicroseconds*/) 
		{
			HLSSink* sink = (HLSSink*)clientData;
			sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
		}

		void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
		{
			if (numTruncatedBytes > 0) 
			{
				envir() << "FileSink::afterGettingFrame(): The input frame data was too large for our buffer size \n";
			}
			if (m_os.is_open())
			{
				if (m_slice != (presentationTime.tv_sec/10))
				{
					m_os.close();
				}
			}
			if (!m_os.is_open())
			{
				m_slice = presentationTime.tv_sec/10;
				if (m_firstslice == 0)
				{
					m_firstslice = m_slice;
				}
				std::ostringstream os;
				os << m_slice << ".ts";				
				m_os.open(os.str().c_str());
			}
			if (m_os.is_open())
			{
				m_os.write((char*)m_buffer, frameSize);
			}

			continuePlaying();
		}
		
	private:
		unsigned char * m_buffer;
		unsigned int    m_bufferSize;
		std::ofstream   m_os;
	public:
		unsigned int    m_slice;
		unsigned int    m_firstslice;
};

class HLSServerMediaSubsession : public OnDemandServerMediaSubsession , public BaseServerMediaSubsession
{
	public:
		static HLSServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format)
		{
			return new HLSServerMediaSubsession(env,replicator,format);
		}
		
	protected:
		HLSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format) 
				: OnDemandServerMediaSubsession(env, False), BaseServerMediaSubsession(replicator), m_format(format) 
		{
			// Create a source
			FramedSource* source = replicator->createStreamReplica();			
			FramedSource* videoSource = createSource(env, source, format);
			
			// Start Playing the Sink
			m_videoSink = HLSSink::createNew(env, 65535);
			m_videoSink->startPlaying(*videoSource, NULL, NULL);			
		}
			
		virtual FramedSource* createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
		{
			FramedSource* source = m_replicator->createStreamReplica();
			return createSource(envir(), source, m_format);
		}					
		
		virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
		{
			return createSink(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic, m_format);
		}
		
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource);	
		virtual float duration() const { 
			std::cout << "duration " << (m_videoSink->m_slice - m_videoSink->m_firstslice)*10 << std::endl;
			return (m_videoSink->m_slice - m_videoSink->m_firstslice)*10; 
		}
		virtual void seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes) 
		{
			m_slice = seekNPT / 10;
			seekNPT = m_slice*10;
			std::ostringstream os;
			os << m_slice+m_videoSink->m_firstslice << ".ts";
			struct stat sb;
			int statResult = stat(os.str().c_str(), &sb);
			if (statResult == 0) 
			{
				numBytes = sb.st_size;
			}			
		}	
		virtual FramedSource* getStreamSource(void* streamToken) 
		{
			std::ostringstream os;
			os << m_slice+m_videoSink->m_firstslice << ".ts";
			return ByteStreamFileSource::createNew(envir(), os.str().c_str());			
		}					
					
	protected:
		const std::string m_format;
		unsigned int      m_slice;
		HLSSink *         m_videoSink;
};

#endif
