/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MJPEGVideoSource.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                       
** MJPEG Source for RTSP server 
**                                                                                    
** -------------------------------------------------------------------------*/

#include "MJPEGVideoSource.h"

      
void MJPEGVideoSource::afterGettingFrame(unsigned frameSize,unsigned numTruncatedBytes,struct timeval presentationTime,unsigned durationInMicroseconds)
{
	int headerSize = 0;
	fFrameSize = 0;
		  
	unsigned int i = 0; 
	while ( (i<frameSize) && (headerSize==0) ) {
	    // SOF
	    if ( ((i+11) < frameSize)  && (fTo[i] == 0xFF) && (fTo[i+1] == 0xC0) ) {
		int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
		LOG(DEBUG) << "SOF length:" << length;

		m_height = (fTo[i+5]<<5)|(fTo[i+6]>>3);
		m_width  = (fTo[i+7]<<5)|(fTo[i+8]>>3);

		int hv_subsampling = fTo[i+11];
		if (hv_subsampling == 0x21 ) {
		    m_type = 0; // JPEG 4:2:2
		} else if (hv_subsampling == 0x22 ) {
		    m_type = 1; // JPEG 4:2:0
		} else {
		    LOG(NOTICE) << "not managed sampling:0x" << std::hex << hv_subsampling;
		    m_type = 255;
		}
		    
		int precision = fTo[i+4];		        
		LOG(INFO) << "width:" << (int)(m_width<<3) << " height:" << (int)(m_height<<3) << " type:"<< (int)m_type << " precision:" << precision;

		i+=length+2;
	    }
	    // DQT
	    else if ( ( (i+5+64) < frameSize)  && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDB)) {
		int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
		LOG(DEBUG) << "DQT length:" << length;

		unsigned int precision = (fTo[i+4]&0xf0)<<4;
		unsigned int quantIdx  = fTo[i+4]&0x0f;
		unsigned int quantSize = length-3;
		if (quantSize*quantIdx+quantSize <= sizeof(m_qTable)) {
		    memcpy(m_qTable + quantSize*quantIdx, fTo + i + 5, quantSize);
		    LOG(NOTICE) << "Quantization table idx:" << quantIdx << " precision:" << precision << " size:" << quantSize << " total size:" << m_qTableSize;
		    if (quantSize*quantIdx+quantSize > m_qTableSize) {
			m_qTableSize = quantSize*quantIdx+quantSize;
		    }
		}

		i+=length+2;	       
	    }
	    // SOS
	    else if ( ((i+1) < frameSize) && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDA) ) {            
		int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
		LOG(DEBUG) << "SOS length:" << length;                
		
		headerSize = i+length+2;                
	    } else {
		i++;
	    }
	}

	if (headerSize != 0) {
	    LOG(DEBUG) << "headerSize:" << headerSize;
	    fFrameSize = frameSize - headerSize;
	    memmove( fTo, fTo + headerSize, fFrameSize );
	} else {
	    LOG(NOTICE) << "Bad header => dropping frame";
	}

	fNumTruncatedBytes = numTruncatedBytes;
	fPresentationTime = presentationTime;
	fDurationInMicroseconds = durationInMicroseconds;
	afterGetting(this);
}

u_int8_t const* MJPEGVideoSource::quantizationTables( u_int8_t& precision, u_int16_t& length )
{
	length = 0;
	precision = 0;
	if (m_qTableSize > 0)
	{
		length = m_qTableSize;
		precision = m_precision;
	}
	return m_qTable;            
}
