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

#ifdef HAVE_ALSA

#include "ALSACapture.h"
#include "NullAudioCompressor.h"

ALSACapture* ALSACapture::createNew(const ALSACaptureParameters & params) 
{ 
	LOG(NOTICE) << "params.m_audioCompressor: " << params.m_audioCompressor;

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

ALSACapture::~ALSACapture()
{
	this->close();
}

void ALSACapture::close()
{
	if (m_pcm != NULL)
	{
		snd_pcm_close (m_pcm);
		m_pcm = NULL;
	}
}




ALSACapture::ALSACapture(const ALSACaptureParameters & params) : m_pcm(NULL), m_bufferSize(0), m_periodSize(0), m_params(params)
{
	LOG(NOTICE) << "Open ALSA device: \"" << params.m_devName << "\"";
	
	snd_pcm_hw_params_t *hw_params = NULL;
	int err = 0;
	
	// open PCM device
	if ((err = snd_pcm_open (&m_pcm, m_params.m_devName.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
		LOG(ERROR) << "cannot open audio device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
	}
				
	// configure hw_params
	else if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
		LOG(ERROR) << "cannot allocate hardware parameter structure device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params_any (m_pcm, hw_params)) < 0) {
		LOG(ERROR) << "cannot initialize hardware parameter structure device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	else if ((err = snd_pcm_hw_params_set_access (m_pcm, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		LOG(ERROR) << "cannot set access type device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if (this->configureFormat(hw_params) < 0) {
		this->close();
	}
	else if ((err = snd_pcm_hw_params_set_rate_near (m_pcm, hw_params, &m_params.m_sampleRate, 0)) < 0) {
		LOG(ERROR) << "cannot set sample rate device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params_set_channels (m_pcm, hw_params, m_params.m_channels)) < 0) {
		LOG(ERROR) << "cannot set channel count device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	else if ((err = snd_pcm_hw_params (m_pcm, hw_params)) < 0) {
		LOG(ERROR) << "cannot set parameters device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	
	// get buffer size
	else if ((err = snd_pcm_get_params(m_pcm, &m_bufferSize, &m_periodSize)) < 0) {
		LOG(ERROR) << "cannot get parameters device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}
	
	// start capture
	else if ((err = snd_pcm_prepare (m_pcm)) < 0) {
		LOG(ERROR) << "cannot prepare audio interface for use device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	else if ((err = snd_pcm_start (m_pcm)) < 0) {
		LOG(ERROR) << "cannot start audio interface for use device: " << m_params.m_devName << " error:" <<  snd_strerror (err);
		this->close();
	}			
	
	LOG(NOTICE) << "ALSA device: \"" << m_params.m_devName << "\" buffer_size:" << m_bufferSize << " period_size:" << m_periodSize << " rate:" << m_params.m_sampleRate;


	// m_params.m_audioCompressor->init(
	// 	m_params.m_channels,
	// 	m_params.m_sampleRate
	// );

}

/*
 * Attempts to set the ALSA hardware device to the format specified.
 */
int ALSACapture::configureFormat(snd_pcm_hw_params_t *hw_params) {
	
	// try to set format, widht, height
	std::list<snd_pcm_format_t>::iterator it;
	for (it = m_params.m_formatList.begin(); it != m_params.m_formatList.end(); ++it) {
		snd_pcm_format_t format = *it;
		int err = snd_pcm_hw_params_set_format (m_pcm, hw_params, format);
		if (err < 0) {
			LOG(NOTICE) << "cannot set sample format device: " << m_params.m_devName << " to:" << format << " error:" <<  snd_strerror (err);
		} else {
			LOG(NOTICE) << "set sample format device: " << m_params.m_devName << " to:" << format << " ok";
			m_fmt = format;
			// m_fmt = SND_PCM_FORMAT_MPEG;
			return 0;
		}
	}
	return -1;
}

size_t ALSACapture::read(char* buffer, size_t bufferSize)
{
	size_t size = 0;
	int fmt_phys_width_bytes = 0;

    // Worst case estimate of how big the MP3 encoded size will be
    // Based on http://www.mit.edu/afs.new/sipb/user/golem/tmp/lame3.70/API
    int mp3buf_size = 1.25 * m_periodSize + 7200;
    int bytescopied = 0;

    char encoded_mp3_data[mp3buf_size];

	// If there is no ALSA capture device for some reason just return 0 bytes read
	if (m_pcm == 0) {
		return 0;
	}

	int fmt_phys_width_bits = snd_pcm_format_physical_width(m_fmt);
	fmt_phys_width_bytes = fmt_phys_width_bits / 8;

	snd_pcm_sframes_t num_pcm_frames = snd_pcm_readi (m_pcm, buffer, m_periodSize*fmt_phys_width_bytes);
	LOG(DEBUG) << "ALSA buffer in_size:" << m_periodSize*fmt_phys_width_bytes << " read_size:" << num_pcm_frames;

	if (num_pcm_frames <= 0) {
		return 0;
	}

	size = num_pcm_frames;


	if (m_params.m_audioCompressor != NULL) {
		// LOG(NOTICE) << "m_params.m_audioCompressor: " << m_params.m_audioCompressor;
		bytescopied = m_params.m_audioCompressor->compress(
			(short *)buffer,
			num_pcm_frames,
			(char *)encoded_mp3_data,
			mp3buf_size
		);

		memcpy(buffer, encoded_mp3_data, bytescopied);
		return bytescopied;
	}
	else {

		// swap if capture in not in network order
		if (!snd_pcm_format_big_endian(m_fmt)) {
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
		return size * m_params.m_channels * fmt_phys_width_bytes;

	}

	return 0;
}
		
int ALSACapture::getFd()
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
		
#endif


