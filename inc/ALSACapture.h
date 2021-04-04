/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ALSACapture.h
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** ALSA capture overide of V4l2Capture
**                                                                                    
** -------------------------------------------------------------------------*/

#ifndef ALSA_CAPTURE
#define ALSA_CAPTURE

#include <list>

#include <alsa/asoundlib.h>
#include "logger.h"

#include "DeviceInterface.h"

struct ALSACaptureParameters 
{
	ALSACaptureParameters(const char* devname, const std::list<snd_pcm_format_t> & formatList, unsigned int sampleRate, unsigned int channels, int verbose) : 
		m_devName(devname), m_formatList(formatList), m_sampleRate(sampleRate), m_channels(channels), m_verbose(verbose) {
			
	}
		
	std::string      m_devName;
	std::list<snd_pcm_format_t> m_formatList;		
	unsigned int     m_sampleRate;
	unsigned int     m_channels;
	int              m_verbose;
};

class ALSACapture  : public DeviceInterface
{
	public:
		static ALSACapture* createNew(const ALSACaptureParameters & params) ;
		virtual ~ALSACapture();
		void close();
	
	protected:
		ALSACapture(const ALSACaptureParameters & params);
		int configureFormat(snd_pcm_hw_params_t *hw_params);
			
	public:
		virtual size_t read(char* buffer, size_t bufferSize);		
		virtual int getFd();
		virtual unsigned long getBufferSize()   { return m_bufferSize;          }	
		
		virtual unsigned long getSampleRate()   { return m_params.m_sampleRate; }
		virtual unsigned long getChannels  ()   { return m_params.m_channels;   }
		virtual int           getAudioFormat () { return m_fmt;                 }
		
	private:
		snd_pcm_t*            m_pcm;
		unsigned long         m_bufferSize;
		unsigned long         m_periodSize;
		ALSACaptureParameters m_params;
		snd_pcm_format_t      m_fmt;
};

#endif


