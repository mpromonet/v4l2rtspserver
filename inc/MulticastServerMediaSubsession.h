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



