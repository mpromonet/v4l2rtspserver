/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#pragma once

#include "ServerMediaSubsession.h"

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
								, StreamReplicator* replicator);
		
	protected:
		MulticastServerMediaSubsession(UsageEnvironment& env
								, struct in_addr destinationAddress
								, Port rtpPortNum, Port rtcpPortNum
								, int ttl
								, StreamReplicator* replicator) 
				: PassiveServerMediaSubsession(*this->createRtpSink(env, destinationAddress, rtpPortNum, rtcpPortNum, ttl, replicator)
											  , m_rtcpInstance)
				, BaseServerMediaSubsession(replicator) {};			

		virtual char const* sdpLines() ;
		virtual char const* getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource);
		RTPSink* createRtpSink(UsageEnvironment& env
								, struct in_addr destinationAddress
								, Port rtpPortNum, Port rtcpPortNum
								, int ttl
								, StreamReplicator* replicator);
		
	protected:
		RTPSink*      m_rtpSink;
		RTCPInstance* m_rtcpInstance;
		std::string   m_SDPLines;
};



