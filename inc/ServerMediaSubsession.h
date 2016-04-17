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

#include <sys/stat.h>

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <map>

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
class HLSServerMediaSubsession : public UnicastServerMediaSubsession
{
	class HLSSink : public MediaSink
	{
		public:
			static HLSSink* createNew(UsageEnvironment& env, unsigned int bufferSize, unsigned int sliceDuration) 
			{
				return new HLSSink(env, bufferSize, sliceDuration);
			}
			
		protected:
			HLSSink(UsageEnvironment& env, unsigned bufferSize, unsigned int sliceDuration) : MediaSink(env), m_bufferSize(bufferSize), m_refTime(0), m_sliceDuration(sliceDuration)
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
							 unsigned durationInMicroseconds) 
			{
				HLSSink* sink = (HLSSink*)clientData;
				sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
			}

			void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
			{
				if (numTruncatedBytes > 0) 
				{
					envir() << "FileSink::afterGettingFrame(): The input frame data was too large for our buffer size \n";
					// realloc a bigger buffer
					m_bufferSize += numTruncatedBytes;
					delete[] m_buffer;
					m_buffer = new unsigned char[m_bufferSize];
				}
				else
				{			
					// append buffer to slice buffer
					if (m_refTime == 0)
					{
						m_refTime = presentationTime.tv_sec;
					}
					unsigned int slice = (presentationTime.tv_sec-m_refTime)/m_sliceDuration;
					std::string& outputBuffer = m_outputBuffers[slice];
					outputBuffer.append((const char*)m_buffer, frameSize);
					
					// remove old buffers
					while (m_outputBuffers.size()>5)
					{
						m_outputBuffers.erase(m_outputBuffers.begin());
					}
				}

				continuePlaying();
			}

		public:
			unsigned int getHLSBufferSize(unsigned int slice)
			{
				unsigned int size = 0;
				std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
				if (it != m_outputBuffers.end())
				{
					size = it->second.size();
				}
				return size;
			}
			
			const char* getHLSBuffer(unsigned int slice)
			{
				const char* content = NULL;
				std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
				if (it != m_outputBuffers.end())
				{
					content = it->second.c_str();
				}
				return content;
			}
			
			unsigned int firstTime()
			{
				unsigned int firstTime = 0;
				if (m_outputBuffers.size() != 0)				
				{
					firstTime = m_outputBuffers.begin()->first;
				}
				return firstTime*m_sliceDuration;
			}
			
			unsigned int duration()
			{
				unsigned int duration = 0;
				if (m_outputBuffers.size() != 0)				
				{
					duration = m_outputBuffers.rbegin()->first - m_outputBuffers.begin()->first;
				}
				return (duration)*m_sliceDuration;
			}
			unsigned int getSliceDuration()
			{
				return m_sliceDuration;
			}
			
		private:
			unsigned char *                    m_buffer;
			unsigned int                       m_bufferSize;
			std::map<unsigned int,std::string> m_outputBuffers;
			unsigned int                       m_refTime;
			unsigned int                       m_sliceDuration;
	};
	
	public:
		static HLSServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format)
		{
			return new HLSServerMediaSubsession(env,replicator,format);
		}
		
	protected:
		HLSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format) 
				: UnicastServerMediaSubsession(env, replicator, format) 
		{
			// Create a source
			FramedSource* source = replicator->createStreamReplica();			
			FramedSource* videoSource = createSource(env, source, format);
			
			// Start Playing the HLS Sink
			m_hlsSink = HLSSink::createNew(env, OutPacketBuffer::maxSize, 10);
			m_hlsSink->startPlaying(*videoSource, NULL, NULL);			
		}
		virtual ~HLSServerMediaSubsession()
		{
			Medium::close(m_hlsSink);
		}
			
		virtual float getCurrentNPT(void* streamToken)
		{
			return (m_hlsSink->firstTime());
		}
		virtual float duration() const 
		{ 
			return (m_hlsSink->duration()); 
		}
		virtual void seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes) 
		{
			m_slice = seekNPT / m_hlsSink->getSliceDuration();
			seekNPT = m_slice * m_hlsSink->getSliceDuration();
			numBytes = m_hlsSink->getHLSBufferSize(m_slice);
			std::cout << "seek seekNPT:" << seekNPT << " slice:" << m_slice << " numBytes:" << numBytes << std::endl;
			
		}	
		virtual FramedSource* getStreamSource(void* streamToken) 
		{
			unsigned int size = m_hlsSink->getHLSBufferSize(m_slice);
			u_int8_t* content = new u_int8_t[size];
			memcpy(content, m_hlsSink->getHLSBuffer(m_slice), size);
			return ByteStreamMemoryBufferSource::createNew(envir(), content, size);			
		}					
					
	protected:
		unsigned int      m_slice;
		HLSSink *         m_hlsSink;
};

#endif
