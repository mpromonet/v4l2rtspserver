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
         bool headerOk = false;
         fFrameSize = 0;
                  
         for (unsigned int i = 0; i < frameSize ; ++i)
         {
            // SOF
            if ( (i+11) < frameSize  && fTo[i] == 0xFF && fTo[i+1] == 0xC0 )
            {
               m_height = (fTo[i+5]<<5)|(fTo[i+6]>>3);
               m_width  = (fTo[i+7]<<5)|(fTo[i+8]>>3);
               m_type   = (fTo[i+11] - 0x21);
               LOG(INFO) << "width:" << (int)m_width << " height:" << (int)m_height << " type:"<< (int)m_type;

            }
            // DQT
            if ( (i+5+64) < frameSize && (fTo[i] == 0xFF) && (fTo[i+1] == 0xDB))
            {
               unsigned int quantSize = fTo[i+3]-4;
               unsigned int quantIdx = fTo[i+4];
               if (quantSize*quantIdx+quantSize <= sizeof(m_qTable))
               {
                  memcpy(m_qTable + quantSize*quantIdx, fTo + i + 5, quantSize);
                  if (quantSize*quantIdx+quantSize > m_qTableSize)
                  {
                     m_qTableSize = quantSize*quantIdx+quantSize;
                     LOG(NOTICE) << "Quantization table idx:" << quantIdx << " size:" << quantSize << " total size:" << m_qTableSize;
                  }
               }
            }
            // End of header
            if ( (i+1) < frameSize && fTo[i] == 0x3F && fTo[i+1] == 0x00 )
            {
               headerOk = true;
               headerSize = i+2;
               break;
            }
         }

         if (headerOk)
         {
            fFrameSize = frameSize - headerSize;
            memmove( fTo, fTo + headerSize, fFrameSize );
         }
         else
         {
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
         }
         return m_qTable;            
      }

   protected:
      MJPEGVideoSource(UsageEnvironment& env, FramedSource* source) : JPEGVideoSource(env),
         m_inputSource(source),
         m_width(0), m_height(0), m_qTableSize(0),
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
      u_int8_t      m_type;
};
