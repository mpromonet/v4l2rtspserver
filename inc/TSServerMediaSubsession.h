/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** TSServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#pragma once

#include <map>
#include "UnicastServerMediaSubsession.h"
#include "MemoryBufferSink.h"

// -----------------------------------------
//    ServerMediaSubsession for HLS
// -----------------------------------------
class TSServerMediaSubsession : public UnicastServerMediaSubsession
{
	public:
		static TSServerMediaSubsession* createNew(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format, unsigned int sliceDuration)
		{
			return new TSServerMediaSubsession(env, replicator, format, sliceDuration);
		}
		
	protected:
		TSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format, unsigned int sliceDuration); 
		virtual ~TSServerMediaSubsession();
			
		virtual float         getCurrentNPT(void* streamToken);
		virtual float         duration() const ;
		virtual void          seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes);
		virtual FramedSource* getStreamSource(void* streamToken);
					
	protected:
		unsigned int      m_slice;
		MemoryBufferSink* m_hlsSink;
};


