/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** ALSACapture.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** ALSA capture overide of V4l2Capture
**                                                                                    
** -------------------------------------------------------------------------*/

#ifndef ALSA_CAPTURE
#define ALSA_CAPTURE

#include <alsa/asoundlib.h>

struct ALSACaptureParameters 
{
	ALSACaptureParameters(const char* devname, unsigned int sampleRate, unsigned int channels, int verbose) : 
		m_devName(devname), m_sampleRate(sampleRate), m_channels(channels), m_verbose(verbose) {};
		
	std::string m_devName;
	unsigned int m_sampleRate;
	unsigned int m_channels;
	int m_verbose;
};

class ALSACapture 
{
	public:
		static ALSACapture* createNew(const ALSACaptureParameters & params) 
		{ 
			ALSACapture* capture = new ALSACapture(params);
			if (capture) 
			{
				if (capture->getFd() == -1) 
				{
					delete capture;
					capture = NULL;
				}
			}
			return capture; 
		}
		virtual ~ALSACapture()
		{
			if (m_pcm != NULL)
			{
				snd_pcm_close (m_pcm);
			}
		}
	
	protected:
		ALSACapture(const ALSACaptureParameters & params) : m_pcm(NULL), m_bufferSize(0), m_periodSize(0)
		{
			snd_pcm_hw_params_t *hw_params = NULL;
			unsigned int rate = params.m_sampleRate;
			int err = 0;
			
			// open PCM device
			if ((err = snd_pcm_open (&m_pcm, params.m_devName.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
				fprintf (stderr, "cannot open audio device %s (%s)\n", params.m_devName.c_str(), snd_strerror (err));
			}
						
			// configure hw_params
			else if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
				fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params_any (m_pcm, hw_params)) < 0) {
				fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n", snd_strerror (err));
			}			
			else if ((err = snd_pcm_hw_params_set_access (m_pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
				fprintf (stderr, "cannot set access type (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params_set_format (m_pcm, hw_params, SND_PCM_FORMAT_S16_BE)) < 0) {
				fprintf (stderr, "cannot set sample format (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params_set_rate_near (m_pcm, hw_params, &rate, 0)) < 0) {
				fprintf (stderr, "cannot set sample rate (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params_set_channels (m_pcm, hw_params, params.m_channels)) < 0) {
				fprintf (stderr, "cannot set channel count (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params (m_pcm, hw_params)) < 0) {
				fprintf (stderr, "cannot set parameters (%s)\n", snd_strerror (err));
			}
			
			// get buffer size
			else if ((err = snd_pcm_get_params(m_pcm, &m_bufferSize, &m_periodSize)) < 0) {
				fprintf (stderr, "cannot get parameters (%s)\n", snd_strerror (err));
			}
			
			// start capture
			else if ((err = snd_pcm_prepare (m_pcm)) < 0) {
				fprintf (stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror (err));
			}			
			else if ((err = snd_pcm_start (m_pcm)) < 0) {
				fprintf (stderr, "cannot start audio interface for use (%s)\n", snd_strerror (err));
			}			
		}
			
	public:
		virtual size_t read(char* buffer, size_t bufferSize)
		{
			return snd_pcm_readi (m_pcm, buffer, m_periodSize);
		}
		
		virtual int getFd()
		{
			unsigned int nbfs = 1;
			struct pollfd pfds[nbfs]; 
			pfds[0].fd = -1;
			
			if (m_pcm != 0)
			{
				int count = snd_pcm_poll_descriptors_count (m_pcm);
				int err = snd_pcm_poll_descriptors(m_pcm, pfds, count);
				if (err < 0) {
					fprintf (stderr, "cannot snd_pcm_poll_descriptors (%s)\n", snd_strerror (err));
				}
			}
			return pfds[0].fd;
		}
		
		unsigned long getBufferSize() { return m_bufferSize; };
		
	private:
		snd_pcm_t*     m_pcm;
		unsigned long  m_bufferSize;
		unsigned long  m_periodSize;
	
};

#endif


