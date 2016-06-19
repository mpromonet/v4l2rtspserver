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

#include <alsa/asoundlib.h>

class ALSACapture : public V4l2Capture
{
	public:
		static ALSACapture* createNew(V4L2DeviceParameters params) { return new ALSACapture(params); }
		virtual ~ALSACapture()
		{
			if (m_pcm != NULL)
			{
				snd_pcm_close (m_pcm);
			}
		}
	
	protected:
		ALSACapture(V4L2DeviceParameters params) : V4l2Device(params,V4L2_BUF_TYPE_VIDEO_CAPTURE), V4l2Capture(params), m_pcm(NULL)
		{
			snd_pcm_hw_params_t *hw_params = NULL;
			int err = 0;
			unsigned int rate = 44100;

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
			else if ((err = snd_pcm_hw_params_set_channels (m_pcm, hw_params, 2)) < 0) {
				fprintf (stderr, "cannot set channel count (%s)\n", snd_strerror (err));
			}
			else if ((err = snd_pcm_hw_params (m_pcm, hw_params)) < 0) {
				fprintf (stderr, "cannot set parameters (%s)\n", snd_strerror (err));
			}
			// start capture
			else if ((err = snd_pcm_prepare (m_pcm)) < 0) {
				fprintf (stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror (err));
			}			
		}
			
	public:
		virtual size_t read(char* buffer, size_t bufferSize)
		{
			size_t ret = snd_pcm_readi (m_pcm, buffer, bufferSize);
			fprintf (stderr, "snd_pcm_readi size:%ld\n", ret);
			return ret;
		}
		
		virtual bool isReady() { return (m_pcm != NULL); };
		
	private:
		snd_pcm_t* m_pcm;
	
};

