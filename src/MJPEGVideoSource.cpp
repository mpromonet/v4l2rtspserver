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
		  
	unsigned int i = 0; 
	while ( (i<frameSize) && (headerSize==0) ) {
	    if ( ((i+11) < frameSize)  && (fTo[i] == 0xFF) && (fTo[i+1] == 0xC0) ) {
		// SOF
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
		
	    } else if (((i+5) < frameSize) && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDB)) {
		// DQT
		int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
		LOG(DEBUG) << "DQT length:" << length;

		int qtable_length = length-2;
		unsigned int qtable_position = i+4;
		while (qtable_length > 0) {
		    LOG(DEBUG) << "DQT qtable_length:" << qtable_length;
		    unsigned int precision = (fTo[qtable_position]&0xf0)<<4;
		    unsigned int quantIdx  = fTo[qtable_position]&0x0f;
		    unsigned int quantSize =  64*(precision+1);
		    if (quantSize*quantIdx+quantSize <= sizeof(m_qTable)) {
			if ( (qtable_position+quantSize) < frameSize) {
			    memcpy(m_qTable + quantSize*quantIdx, fTo + qtable_position + 1, quantSize);
			    LOG(DEBUG) << "Quantization table idx:" << quantIdx << " precision:" << precision << " size:" << quantSize << " total size:" << m_qTableSize;
			    if (quantSize*quantIdx+quantSize > m_qTableSize) {
				m_qTableSize = quantSize*quantIdx+quantSize;
			    }
			}
		    }
		    qtable_length -= quantSize+1;
		    qtable_position += quantSize+1;
		}

		i+=length+2;	       
		
	    } else if ( ((i+5) < frameSize) && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDD) ) {
		// DRI
		int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
		m_restartInterval = (fTo[i+4]<<8)|(fTo[i+5]);
		LOG(DEBUG) << "DRI restartInterval:" << m_restartInterval;              
		    
		i+=length+2;	       
	    
	    } else if ( ((i+3) < frameSize) && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDA) ) {            
		// SOS
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
	    fFrameSize = 0;
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
