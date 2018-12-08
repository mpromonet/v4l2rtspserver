/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** AddH26xMarkerFilter.h
** 
** -------------------------------------------------------------------------*/

#pragma once

class AddH26xMarkerFilter : public FramedFilter {
	public:
		AddH26xMarkerFilter (UsageEnvironment& env, FramedSource* inputSource): FramedFilter(env, inputSource) {
			m_bufferSize = OutPacketBuffer::maxSize;
			m_buffer = new unsigned char[m_bufferSize];
		}
		virtual ~AddH26xMarkerFilter () {
			delete [] m_buffer;
		}
		
	private:

		static void afterGettingFrame(void* clientData, unsigned frameSize,
						 unsigned numTruncatedBytes,
						 struct timeval presentationTime,
						 unsigned durationInMicroseconds) {
			AddH26xMarkerFilter* sink = (AddH26xMarkerFilter*)clientData;
			sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime);
		}
				
		void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes, struct timeval presentationTime) 
		{
			fPresentationTime = presentationTime;
			fDurationInMicroseconds = 0;
			if (numTruncatedBytes > 0) 
			{
				envir() << "AddH26xMarkerFilter::afterGettingFrame(): The input frame data was too large for our buffer size truncated:" << numTruncatedBytes << " bufferSize:" << m_bufferSize << "\n";
				m_bufferSize += numTruncatedBytes;
				delete[] m_buffer;
				m_buffer = new unsigned char[m_bufferSize];
				fFrameSize = 0;
			} else {
				char marker[] = {0,0,0,1};
				fFrameSize = frameSize + sizeof(marker);
				if (fFrameSize > fMaxSize) {
					fNumTruncatedBytes = fFrameSize - fMaxSize; 
					envir() << "AddH26xMarkerFilter::afterGettingFrame(): buffer too small truncated:" << fNumTruncatedBytes << " bufferSize:" << fFrameSize << "\n";
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


