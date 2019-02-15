/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** TSServerMediaSubsession.cpp
** 
** -------------------------------------------------------------------------*/

#include "TSServerMediaSubsession.h"
#include "AddH26xMarkerFilter.h"

TSServerMediaSubsession::TSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* videoreplicator, const std::string& videoformat, StreamReplicator* audioreplicator, const std::string& audioformat, unsigned int sliceDuration) 
		: UnicastServerMediaSubsession(env, videoreplicator, "video/MP2T"), m_slice(0)
{
	// Create a source
	FramedSource* source = videoreplicator->createStreamReplica();
	
	
	if (videoformat == "video/H264") {
		// add marker
		FramedSource* filter = new AddH26xMarkerFilter(env, source);
		// mux to TS		
		MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(env);
		muxer->addNewVideoSource(filter, 5);
		source = muxer;
	} else if (videoformat == "video/H265") {
		// add marker
		FramedSource* filter = new AddH26xMarkerFilter(env, source);
		// mux to TS		
		MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(env);
		muxer->addNewVideoSource(filter, 6);
		source = muxer;
	}
	
	FramedSource* videoSource = createSource(env, source, m_format);
	
	// Start Playing the HLS Sink
	m_hlsSink = MemoryBufferSink::createNew(env, OutPacketBuffer::maxSize, sliceDuration);
	m_hlsSink->startPlaying(*videoSource, NULL, NULL);			
}

TSServerMediaSubsession::~TSServerMediaSubsession()
{
	Medium::close(m_hlsSink);
}
	
float TSServerMediaSubsession::getCurrentNPT(void* streamToken)
{
	return (m_hlsSink->firstTime());
}

float TSServerMediaSubsession::duration() const 
{ 
	return (m_hlsSink->duration()); 
}

void TSServerMediaSubsession::seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes) 
{
	m_slice = seekNPT / m_hlsSink->getSliceDuration();
	seekNPT = m_slice * m_hlsSink->getSliceDuration();
	numBytes = m_hlsSink->getBufferSize(m_slice);
	std::cout << "seek seekNPT:" << seekNPT << " slice:" << m_slice << " numBytes:" << numBytes << std::endl;	
}	

FramedSource* TSServerMediaSubsession::getStreamSource(void* streamToken) 
{
	FramedSource* source = NULL;
	
	std::string buffer = m_hlsSink->getBuffer(m_slice);
	unsigned int size = buffer.size();
	if ( size != 0 ) {
		u_int8_t* content = new u_int8_t[size];
		memcpy(content, buffer.c_str(), size);
		source = ByteStreamMemoryBufferSource::createNew(envir(), content, size);			
	}
	return source;			
}					
