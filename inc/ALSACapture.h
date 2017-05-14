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
			this->close();
		}
		void close()
		{
			if (m_pcm != NULL)
			{
				snd_pcm_close (m_pcm);
				m_pcm = NULL;
			}
		}
	
	protected:
		ALSACapture(const ALSACaptureParameters & params) : m_pcm(NULL), m_bufferSize(0), m_periodSize(0), m_params(params)
		{
			LOG(NOTICE) << "Open ALSA device: \"" << params.m_devName << "\"";
			
			snd_pcm_hw_params_t *hw_params = NULL;
			unsigned int rate = params.m_sampleRate;
			int err = 0;
			
			// open PCM device
			if ((err = snd_pcm_open (&m_pcm, params.m_devName.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
				LOG(ERROR) << "cannot open audio device: " << params.m_devName << " error:" <<  snd_strerror (err);
			}
						
			// configure hw_params
			else if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
				LOG(ERROR) << "cannot allocate hardware parameter structure device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			else if ((err = snd_pcm_hw_params_any (m_pcm, hw_params)) < 0) {
				LOG(ERROR) << "cannot initialize hardware parameter structure device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}			
			else if ((err = snd_pcm_hw_params_set_access (m_pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
				LOG(ERROR) << "cannot set access type device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			else if ((err = snd_pcm_hw_params_set_format (m_pcm, hw_params, params.m_fmt)) < 0) {
				LOG(ERROR) << "cannot set sample format device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			else if ((err = snd_pcm_hw_params_set_rate_near (m_pcm, hw_params, &rate, 0)) < 0) {
				LOG(ERROR) << "cannot set sample rate device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			else if ((err = snd_pcm_hw_params_set_channels (m_pcm, hw_params, params.m_channels)) < 0) {
				LOG(ERROR) << "cannot set channel count device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			else if ((err = snd_pcm_hw_params (m_pcm, hw_params)) < 0) {
				LOG(ERROR) << "cannot set parameters device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			
			// get buffer size
			else if ((err = snd_pcm_get_params(m_pcm, &m_bufferSize, &m_periodSize)) < 0) {
				LOG(ERROR) << "cannot get parameters device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}
			
			// start capture
			else if ((err = snd_pcm_prepare (m_pcm)) < 0) {
				LOG(ERROR) << "cannot prepare audio interface for use device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}			
			else if ((err = snd_pcm_start (m_pcm)) < 0) {
				LOG(ERROR) << "cannot start audio interface for use device: " << params.m_devName << " error:" <<  snd_strerror (err);
				this->close();
			}			
			
			LOG(NOTICE) << "ALSA device: \"" << params.m_devName << "\" buffer_size:" << m_bufferSize << " period_size:" << m_periodSize;
		}
			
	public:
		virtual size_t read(char* buffer, size_t bufferSize)
		{
			size_t size = 0;
			if (m_pcm != 0)
			{
				size = snd_pcm_readi (m_pcm, buffer, m_periodSize);
				
				LOG(DEBUG) << "ALSA buffer size:" << m_bufferSize << " " << m_periodSize*m_params.m_channels << " " << size;
				
				// swap if capture in not in network order
				if (!snd_pcm_format_big_endian(m_params.m_fmt)) {
					
					
					int fmt_phys_width_bits = snd_pcm_format_physical_width(m_params.m_fmt);
					int fmt_phys_width_bytes = fmt_phys_width_bits / 8;			
				
					for(unsigned int i = 0; i < size; i++){
						char * ptr = &buffer[i * fmt_phys_width_bytes * m_params.m_channels];
						
						for(unsigned int j = 0; j < m_params.m_channels; j++){
							ptr += j * fmt_phys_width_bytes;
							for (int k = 0; k < fmt_phys_width_bytes/2; k++) {
								char byte = ptr[k];
								ptr[k] = ptr[fmt_phys_width_bytes - 1 - k];
								ptr[fmt_phys_width_bytes - 1 - k] = byte; 
							}
						}
					}			
				}
			}
			return size*m_params.m_channels;
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
		snd_pcm_t*            m_pcm;
		unsigned long         m_bufferSize;
		unsigned long         m_periodSize;
		ALSACaptureParameters m_params;
};

#endif


