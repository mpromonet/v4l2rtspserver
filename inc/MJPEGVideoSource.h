/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MJPEGVideoSource.h
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
**                                                                                    
** -------------------------------------------------------------------------*/

#include "logger.h"
#include "JPEGVideoSource.hh"

class MJPEGVideoSource : public JPEGVideoSource
{
   public:
      static MJPEGVideoSource* createNew (UsageEnvironment& env, FramedSource* source)
      {
         return new MJPEGVideoSource(env,source);
      }
      virtual void doGetNextFrame()
      {
         if (m_inputSource)
            m_inputSource->getNextFrame(fTo, fMaxSize, afterGettingFrameSub, this, FramedSource::handleClosure, this);                     
      }
      virtual void doStopGettingFrames()
      {
         FramedSource::doStopGettingFrames();
         if (m_inputSource)
            m_inputSource->stopGettingFrames();                    
      }
      static void afterGettingFrameSub(void* clientData, unsigned frameSize,unsigned numTruncatedBytes,struct timeval presentationTime,unsigned durationInMicroseconds) 
      {
         MJPEGVideoSource* source = (MJPEGVideoSource*)clientData;
         source->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
      } 
      
      void afterGettingFrame(unsigned frameSize,unsigned numTruncatedBytes,struct timeval presentationTime,unsigned durationInMicroseconds)
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
                LOG(INFO) << "width:" << (int)(m_width<<3) << " height:" << (int)(m_height<<3) << " type:"<< (int)m_type;

                i+=length+2;
            }
            // DQT
            else if ( ( (i+5+64) < frameSize)  && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDB)) {
                int length = (fTo[i+2]<<8)|(fTo[i+3]);		    
                LOG(DEBUG) << "DQT length:" << length;

                m_precision = fTo[i+4]<<4;
                unsigned int quantIdx  = fTo[i+4]&0x0f;
                unsigned int quantSize = length-3;
                if (quantSize*quantIdx+quantSize <= sizeof(m_qTable)) {
                    memcpy(m_qTable + quantSize*quantIdx, fTo + i + 5, quantSize);
                    if (quantSize*quantIdx+quantSize > m_qTableSize) {
                        m_qTableSize = quantSize*quantIdx+quantSize;
                        LOG(NOTICE) << "Quantization table idx:" << quantIdx << " precision:" << m_precision << " size:" << quantSize << " total size:" << m_qTableSize;
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
      virtual u_int8_t type() { return m_type; };
      virtual u_int8_t qFactor() { return 128; };
      virtual u_int8_t width() { return m_width; };
      virtual u_int8_t height() { return m_height; };
      u_int8_t const* quantizationTables( u_int8_t& precision, u_int16_t& length )
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

   protected:
      MJPEGVideoSource(UsageEnvironment& env, FramedSource* source) : JPEGVideoSource(env),
         m_inputSource(source),
         m_width(0), m_height(0), m_qTableSize(0), m_precision(0),
         m_type(0)
      {
         memset(&m_qTable,0,sizeof(m_qTable));
      }
      virtual ~MJPEGVideoSource() 
      { 
         Medium::close(m_inputSource); 
      }

      protected:
      FramedSource* m_inputSource;
      u_int8_t      m_width;
      u_int8_t      m_height;
      u_int8_t      m_qTable[128*2];
      unsigned int  m_qTableSize;
      unsigned int  m_precision;
      u_int8_t      m_type;
};
