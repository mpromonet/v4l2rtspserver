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

#include <alsa/asoundlib.h>
#include "logger.h"

struct ALSACaptureParameters 
{
	ALSACaptureParameters(const char* devname, snd_pcm_format_t fmt, unsigned int sampleRate, unsigned int channels, int verbose) : 
		m_devName(devname), m_fmt(fmt), m_sampleRate(sampleRate), m_channels(channels), m_verbose(verbose) {};
		
	std::string      m_devName;
	snd_pcm_format_t m_fmt;
	unsigned int     m_sampleRate;
	unsigned int     m_channels;
	int              m_verbose;
};

class ALSACapture 
{
	public:
		static ALSACapture* createNew(const ALSACaptureParameters & params) ;
		virtual ~ALSACapture();
		void close();
	
	protected:
		ALSACapture(const ALSACaptureParameters & params);
			
	public:
		virtual size_t read(char* buffer, size_t bufferSize);		
		virtual int getFd();
		
		virtual unsigned long getBufferSize() { return m_bufferSize; };
		virtual int getWidth()  {return -1;}
		virtual int getHeight() {return -1;}	
		
		unsigned long getSampleRate() { return m_params.m_sampleRate; };
		unsigned long getChannels  () { return m_params.m_channels; };
		
	private:
		snd_pcm_t*            m_pcm;
		unsigned long         m_bufferSize;
		unsigned long         m_periodSize;
		ALSACaptureParameters m_params;
};

#endif


