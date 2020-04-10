/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MemoryBufferSink.cpp
** 
** -------------------------------------------------------------------------*/

#include "MemoryBufferSink.h"

// -----------------------------------------
//    MemoryBufferSink
// -----------------------------------------
MemoryBufferSink::MemoryBufferSink(UsageEnvironment& env, unsigned bufferSize, unsigned int sliceDuration, unsigned int nbSlices) : MediaSink(env), m_bufferSize(bufferSize), m_refTime(0), m_sliceDuration(sliceDuration), m_nbSlices(nbSlices)
{
	m_buffer = new unsigned char[m_bufferSize];
}

MemoryBufferSink::~MemoryBufferSink() 
{
	delete[] m_buffer;
}


Boolean MemoryBufferSink::continuePlaying() 
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


void MemoryBufferSink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
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
		while (m_outputBuffers.size()>m_nbSlices)
		{
			m_outputBuffers.erase(m_outputBuffers.begin());
		}
	}

	continuePlaying();
}

unsigned int MemoryBufferSink::getBufferSize(unsigned int slice)
{
	unsigned int size = 0;
	std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
	if (it != m_outputBuffers.end())
	{
		size = it->second.size();
	}
	return size;
}

std::string MemoryBufferSink::getBuffer(unsigned int slice)
{
	std::string content;
	std::map<unsigned int,std::string>::iterator it = m_outputBuffers.find(slice);
	if (it != m_outputBuffers.end())
	{
		content = it->second;

	}
	return content;
}

unsigned int MemoryBufferSink::firstTime()
{
	unsigned int firstTime = 0;
	if (m_outputBuffers.size() != 0)				
	{
		firstTime = m_outputBuffers.begin()->first;
	}
	return firstTime*m_sliceDuration;
}

unsigned int MemoryBufferSink::duration()
{
	unsigned int duration = 0;
	if (m_outputBuffers.size() != 0)				
	{
		duration = m_outputBuffers.rbegin()->first - m_outputBuffers.begin()->first;
	}
	return (duration)*m_sliceDuration;
}
