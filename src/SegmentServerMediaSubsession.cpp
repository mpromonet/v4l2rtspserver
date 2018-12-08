/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SegmentServerMediaSubsession.cpp
** 
** -------------------------------------------------------------------------*/

#include <map>

#include "SegmentServerMediaSubsession.h"

// -----------------------------------------
//    ServerMediaSubsession for HLS
// -----------------------------------------
HLSServerMediaSubsession::HLSSink::HLSSink(UsageEnvironment& env, unsigned bufferSize, unsigned int sliceDuration) : MediaSink(env), m_bufferSize(bufferSize), m_refTime(0), m_sliceDuration(sliceDuration)
{
	m_buffer = new unsigned char[m_bufferSize];
}

HLSServerMediaSubsession::HLSSink::~HLSSink() 
{
	delete[] m_buffer;
}


Boolean HLSServerMediaSubsession::HLSSink::continuePlaying() 
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


void HLSServerMediaSubsession::HLSSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
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

unsigned int HLSServerMediaSubsession::HLSSink::getHLSBufferSize(unsigned int slice)
{
	unsigned int size = 0;
	std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
	if (it != m_outputBuffers.end())
	{
		size = it->second.size();
	}
	return size;
}

std::string HLSServerMediaSubsession::HLSSink::getHLSBuffer(unsigned int slice)
{
	std::string content;
	std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
	if (it != m_outputBuffers.end())
	{
		content = it->second;

	}
	return content;
}

unsigned int HLSServerMediaSubsession::HLSSink::firstTime()
{
	unsigned int firstTime = 0;
	if (m_outputBuffers.size() != 0)				
	{
		firstTime = m_outputBuffers.begin()->first;
	}
	return firstTime*m_sliceDuration;
}

unsigned int HLSServerMediaSubsession::HLSSink::duration()
{
	unsigned int duration = 0;
	if (m_outputBuffers.size() != 0)				
	{
		duration = m_outputBuffers.rbegin()->first - m_outputBuffers.begin()->first;
	}
	return (duration)*m_sliceDuration;
}

char marker[] = {0,0,0,1};
class AddMarker : public FramedFilter {
	public:
		AddMarker (UsageEnvironment& env, FramedSource* inputSource): FramedFilter(env, inputSource) {
			m_bufferSize = OutPacketBuffer::maxSize;
			m_buffer = new unsigned char[m_bufferSize];
		}
		virtual ~AddMarker () {
			delete [] m_buffer;
		}
		
	private:

		static void afterGettingFrame(void* clientData, unsigned frameSize,
						 unsigned numTruncatedBytes,
						 struct timeval presentationTime,
						 unsigned durationInMicroseconds) {
			AddMarker* sink = (AddMarker*)clientData;
			sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
		}
				
		void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
		{
			fPresentationTime = presentationTime;
			fDurationInMicroseconds = 0;
			if (numTruncatedBytes > 0) 
			{
				envir() << "AddMarker::afterGettingFrame(): The input frame data was too large for our buffer size truncated:" << numTruncatedBytes << " bufferSize:" << m_bufferSize << "\n";
				m_bufferSize += numTruncatedBytes;
				delete[] m_buffer;
				m_buffer = new unsigned char[m_bufferSize];
				fFrameSize = 0;
			} else {
				fFrameSize = frameSize + sizeof(marker);
				if (fFrameSize > fMaxSize) {
					fNumTruncatedBytes = fFrameSize - fMaxSize; 
					envir() << "AddMarker::afterGettingFrame(): buffer too small truncated:" << fNumTruncatedBytes << " bufferSize:" << fFrameSize << "\n";
				} else {
					fNumTruncatedBytes = 0;
					memcpy(fTo, marker, sizeof(marker));
					memcpy(fTo+sizeof(marker), m_buffer, frameSize); 
				}
			}
			afterGetting(this);
		}
		
		virtual void doGetNextFrame() {
			if (fInputSource != NULL) 
			{
				fInputSource->getNextFrame(m_buffer, m_bufferSize,
						afterGettingFrame, this,
						handleClosure, this);
			}			
		}
		
		unsigned char* m_buffer;
		unsigned int m_bufferSize;
};


HLSServerMediaSubsession::HLSServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, const std::string& format, unsigned int sliceDuration) 
		: UnicastServerMediaSubsession(env, replicator, "video/MP2T"), m_slice(0)
{
	// Create a source
	FramedSource* source = replicator->createStreamReplica();
	
	
	if (format == "video/H264") {
		// add marker
		FramedSource* filter = new AddMarker(env, source);
		// mux to TS		
		MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(env);
		muxer->addNewVideoSource(filter, 5);
		source = muxer;
	} else if (format == "video/H265") {
		// add marker
		FramedSource* filter = new AddMarker(env, source);
		// mux to TS		
		MPEG2TransportStreamFromESSource* muxer = MPEG2TransportStreamFromESSource::createNew(env);
		muxer->addNewVideoSource(filter, 6);
		source = muxer;
	}
	
	FramedSource* videoSource = createSource(env, source, m_format);
	
	// Start Playing the HLS Sink
	m_hlsSink = HLSSink::createNew(env, OutPacketBuffer::maxSize, sliceDuration);
	m_hlsSink->startPlaying(*videoSource, NULL, NULL);			
}

HLSServerMediaSubsession::~HLSServerMediaSubsession()
{
	Medium::close(m_hlsSink);
}
	
float HLSServerMediaSubsession::getCurrentNPT(void* streamToken)
{
	return (m_hlsSink->firstTime());
}

float HLSServerMediaSubsession::duration() const 
{ 
	return (m_hlsSink->duration()); 
}

void HLSServerMediaSubsession::seekStream(unsigned clientSessionId, void* streamToken, double& seekNPT, double streamDuration, u_int64_t& numBytes) 
{
	m_slice = seekNPT / m_hlsSink->getSliceDuration();
	seekNPT = m_slice * m_hlsSink->getSliceDuration();
	numBytes = m_hlsSink->getHLSBufferSize(m_slice);
	std::cout << "seek seekNPT:" << seekNPT << " slice:" << m_slice << " numBytes:" << numBytes << std::endl;	
}	

FramedSource* HLSServerMediaSubsession::getStreamSource(void* streamToken) 
{
	FramedSource* source = NULL;
	
	std::string buffer = m_hlsSink->getHLSBuffer(m_slice);
	unsigned int size = buffer.size();
	if ( size != 0 ) {
		u_int8_t* content = new u_int8_t[size];
		memcpy(content, buffer.c_str(), size);
		source = ByteStreamMemoryBufferSource::createNew(envir(), content, size);			
	}
	return source;			
}					
