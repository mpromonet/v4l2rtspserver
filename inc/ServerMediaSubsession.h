/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ServerMediaSubsession.h
** 
** -------------------------------------------------------------------------*/

#pragma once

#include <sys/stat.h>

#include <string>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>

// live555
#include <liveMedia.hh>

#include <linux/videodev2.h>

#include "DeviceSource.h"
#include "logger.h"

#ifdef HAVE_ALSA   
#include "ALSACapture.h"
#endif

// ---------------------------------
//   BaseServerMediaSubsession
// ---------------------------------
class BaseServerMediaSubsession
{
	public:
		BaseServerMediaSubsession(StreamReplicator* replicator): m_replicator(replicator) {
			V4L2DeviceSource* deviceSource = dynamic_cast<V4L2DeviceSource*>(replicator->inputSource());
			if (deviceSource) {
                DeviceInterface* device = deviceSource->getDevice();
                if (device->getVideoFormat() >= 0) {
    	            m_format = BaseServerMediaSubsession::getVideoRtpFormat(device->getVideoFormat());		
                } else {
    	            m_format = BaseServerMediaSubsession::getAudioRtpFormat(device->getAudioFormat(), device->getSampleRate(), device->getChannels());						
                }
                LOG(NOTICE) << "format:" << m_format;
			}
	    }

		// -----------------------------------------
        //    convert V4L2 pix format to RTP mime
        // -----------------------------------------
        static std::string getVideoRtpFormat(int format)
        {
            std::string rtpFormat;
            switch(format)
            {	
                case V4L2_PIX_FMT_HEVC : rtpFormat = "video/H265"; break;
                case V4L2_PIX_FMT_H264 : rtpFormat = "video/H264"; break;
                case V4L2_PIX_FMT_MJPEG: rtpFormat = "video/JPEG"; break;
                case V4L2_PIX_FMT_JPEG : rtpFormat = "video/JPEG"; break;
                case V4L2_PIX_FMT_VP8  : rtpFormat = "video/VP8" ; break;
                case V4L2_PIX_FMT_VP9  : rtpFormat = "video/VP9" ; break;
                case V4L2_PIX_FMT_YUYV : rtpFormat = "video/RAW" ; break;
                case V4L2_PIX_FMT_UYVY : rtpFormat = "video/RAW" ; break;
            }
            
            return rtpFormat;
        }

        static std::string getAudioRtpFormat(int format, int sampleRate, int channels)
        {
            std::ostringstream os;
#ifdef HAVE_ALSA            
            os << "audio/";
            switch (format) {                
                case SND_PCM_FORMAT_A_LAW:
                    os << "PCMA";
                    break;
                case SND_PCM_FORMAT_MU_LAW:
                    os << "PCMU";
                    break;
                case SND_PCM_FORMAT_S8:
                    os << "L8";
                    break;
                case SND_PCM_FORMAT_S24_BE:
                case SND_PCM_FORMAT_S24_LE:
                    os << "L24";
                    break;
                case SND_PCM_FORMAT_S32_BE:
                case SND_PCM_FORMAT_S32_LE:
                    os << "L32";
                    break;
                case SND_PCM_FORMAT_MPEG:
                    os << "MPEG";
                    break;
                default:
                    os << "L16";
                    break;
            }
            os << "/" << sampleRate << "/" << channels;
#endif            
            return os.str();
        }        
	
	public:
		static FramedSource* createSource(UsageEnvironment& env, FramedSource * videoES, const std::string& format);
		static RTPSink* createSink(UsageEnvironment& env, Groupsock * rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic, const std::string& format, V4L2DeviceSource* source);
		char const* getAuxLine(V4L2DeviceSource* source, RTPSink* rtpSink);
		
	protected:
		StreamReplicator* m_replicator;
		std::string m_format;        
};

