/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.cpp
** 
** -------------------------------------------------------------------------*/


#include "UnicastServerMediaSubsession.h"
#include "DeviceSource.h"

// -----------------------------------------
//    ServerMediaSubsession for Unicast
// -----------------------------------------
UnicastServerMediaSubsession* UnicastServerMediaSubsession::createNew(UsageEnvironment& env, StreamReplicator* replicator) 
{ 
	return new UnicastServerMediaSubsession(env,replicator);
}

UnicastServerMediaSubsession* UnicastServerMediaSubsession::createNew(UsageEnvironment& env, StreamReplicator* replicator, int compressedAudioFmt) 
{
	UnicastServerMediaSubsession* s = new UnicastServerMediaSubsession(env,replicator);

    // If compression is enabled we need to assign the proper RTP format for the audio
    switch(compressedAudioFmt) {
        case COMPRESSED_AUDIO_FMT_NONE:
            break;
        case COMPRESSED_AUDIO_FMT_MP3:
            s->m_format.assign("audio/MPEG");
            break;
    }

    LOG(NOTICE) << "Compressed audio format:" << s->m_format;
	return s;
}

FramedSource* UnicastServerMediaSubsession::createNewStreamSource(unsigned clientSessionId, unsigned& estBitrate)
{
	estBitrate = 500;
	FramedSource* source = m_replicator->createStreamReplica();
	return createSource(envir(), source, m_format);
}
		
RTPSink* UnicastServerMediaSubsession::createNewRTPSink(Groupsock* rtpGroupsock,  unsigned char rtpPayloadTypeIfDynamic, FramedSource* inputSource)
{
	return createSink(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic, m_format, dynamic_cast<V4L2DeviceSource*>(m_replicator->inputSource()));
}
		
char const* UnicastServerMediaSubsession::getAuxSDPLine(RTPSink* rtpSink,FramedSource* inputSource)
{
	return this->getAuxLine(dynamic_cast<V4L2DeviceSource*>(m_replicator->inputSource()), rtpSink);
}
		
