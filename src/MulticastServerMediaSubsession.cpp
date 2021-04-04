/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MulticastServerMediaSubsession.cpp
** 
** -------------------------------------------------------------------------*/

#include "MulticastServerMediaSubsession.h"
#include "DeviceSource.h"

// -----------------------------------------
//    ServerMediaSubsession for Multicast
// -----------------------------------------
MulticastServerMediaSubsession* MulticastServerMediaSubsession::createNew(UsageEnvironment& env
									, struct in_addr destinationAddress
									, Port rtpPortNum, Port rtcpPortNum
									, int ttl
									, StreamReplicator* replicator) 
{ 
	return new MulticastServerMediaSubsession(env, destinationAddress, rtpPortNum, rtcpPortNum, ttl , replicator);
}
		

RTPSink* MulticastServerMediaSubsession::createRtpSink(UsageEnvironment& env
						, struct in_addr destinationAddress
						, Port rtpPortNum, Port rtcpPortNum
						, int ttl
						, StreamReplicator* replicator) {
	// Create a source
	FramedSource* source = replicator->createStreamReplica();			
	FramedSource* videoSource = createSource(env, source, m_format);

	// Create RTP/RTCP groupsock
#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1607644800
	struct in_addr groupAddress = destinationAddress;
#else
	struct sockaddr_storage groupAddress;
	groupAddress.ss_family = AF_INET;
  	((struct sockaddr_in&)groupAddress).sin_addr = destinationAddress;
#endif
	Groupsock* rtpGroupsock = new Groupsock(env, groupAddress, rtpPortNum, ttl);

	// Create a RTP sink
	m_rtpSink = createSink(env, rtpGroupsock, 96, m_format, dynamic_cast<V4L2DeviceSource*>(replicator->inputSource()));

	// Create 'RTCP instance'
	const unsigned maxCNAMElen = 100;
	unsigned char CNAME[maxCNAMElen+1];
	gethostname((char*)CNAME, maxCNAMElen);
	CNAME[maxCNAMElen] = '\0'; 
	Groupsock* rtcpGroupsock = new Groupsock(env, groupAddress, rtcpPortNum, ttl);
	m_rtcpInstance = RTCPInstance::createNew(env, rtcpGroupsock,  500, CNAME, m_rtpSink, NULL);

	// Start Playing the Sink
	m_rtpSink->startPlaying(*videoSource, NULL, NULL);							

	return m_rtpSink;
}

char const* MulticastServerMediaSubsession::sdpLines() 
{
	if (m_SDPLines.empty())
	{
		// Ugly workaround to give SPS/PPS that are get from the RTPSink
#if LIVEMEDIA_LIBRARY_VERSION_INT < 1610928000
		m_SDPLines.assign(PassiveServerMediaSubsession::sdpLines());
#else		 
		m_SDPLines.assign(PassiveServerMediaSubsession::sdpLines(AF_INET));
#endif		
		m_SDPLines.append(getAuxSDPLine(m_rtpSink,NULL));
	}
	return m_SDPLines.c_str();
}

char const* MulticastServerMediaSubsession::getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource)
{
	return this->getAuxLine(dynamic_cast<V4L2DeviceSource*>(m_replicator->inputSource()), rtpSink);
}
		
