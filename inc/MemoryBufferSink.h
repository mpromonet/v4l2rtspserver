/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MemoryBufferSink.h
** 
** Implement a live555 Sink that store time slices in memory
**
** -------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <map>

#include "MediaSink.hh"

class MemoryBufferSink : public MediaSink
{
	public:
		static MemoryBufferSink* createNew(UsageEnvironment& env, unsigned int bufferSize, unsigned int sliceDuration, unsigned int nbSlices = 5) 
		{
			return new MemoryBufferSink(env, bufferSize, sliceDuration, nbSlices);
		}
		
	protected:
		MemoryBufferSink(UsageEnvironment& env, unsigned bufferSize, unsigned int sliceDuration, unsigned int nbSlices);
		virtual ~MemoryBufferSink(); 
		
		virtual Boolean continuePlaying();
	
		static void afterGettingFrame(void* clientData, unsigned frameSize,
						 unsigned numTruncatedBytes,
						 struct timeval presentationTime,
						 unsigned durationInMicroseconds) {
			MemoryBufferSink* sink = (MemoryBufferSink*)clientData;
			sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
		}

		void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime);
		
	public:
		unsigned int getBufferSize(unsigned int slice);
		std::string  getBuffer(unsigned int slice);			
		unsigned int firstTime();
		unsigned int duration();
		unsigned int getSliceDuration() 	{ return m_sliceDuration; }
		
	private:
		unsigned char *                    m_buffer;
		unsigned int                       m_bufferSize;
		std::map<unsigned int,std::string> m_outputBuffers;
		unsigned int                       m_refTime;
		unsigned int                       m_sliceDuration;
		unsigned int                       m_nbSlices;
};
	
