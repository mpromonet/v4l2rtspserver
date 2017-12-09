/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#pragma once

#include <map>
#include "UnicastServerMediaSubsession.h"

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
			HLSSink(UsageEnvironment& env, unsigned bufferSize, unsigned int sliceDuration);
			virtual ~HLSSink(); 
			
			virtual Boolean continuePlaying();
		
			static void afterGettingFrame(void* clientData, unsigned frameSize,
							 unsigned numTruncatedBytes,
							 struct timeval presentationTime,
							 unsigned durationInMicroseconds) {
				HLSSink* sink = (HLSSink*)clientData;
				sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
			}

			void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime);
			
		public:
			unsigned int getHLSBufferSize(unsigned int slice);
			const char*  getHLSBuffer(unsigned int slice);			
			unsigned int firstTime();
			unsigned int duration();
			unsigned int getSliceDuration() 	{ return m_sliceDuration; }
			
		private:
			unsigned char *                    m_buffer;
			unsigned int                       m_bufferSize;
			std::map<unsigned int,std::string> m_outputBuffers;
			unsigned int                       m_refTime;
			unsigned int                       m_sliceDuration;
	};
	
	public:
		static HLSServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format, unsigned int sliceDuration)
		{
			return new HLSServerMediaSubsession(env, replicator, format, sliceDuration);
		}
		
	protected:
		HLSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format, unsigned int sliceDuration); 
		virtual ~HLSServerMediaSubsession();
			
		virtual float         getCurrentNPT(void* streamToken);
		virtual float         duration() const ;
		virtual void          seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes);
		virtual FramedSource* getStreamSource(void* streamToken);
					
	protected:
		unsigned int      m_slice;
		HLSSink *         m_hlsSink;
};


