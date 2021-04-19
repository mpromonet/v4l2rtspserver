/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
**                                                                                    
** -------------------------------------------------------------------------*/

#ifdef HAVE_ALSA

#include "NullAudioCompressor.h"


NullAudioCompressor::NullAudioCompressor(void)
{
	LOG(NOTICE) << "Initializing NullAudioCompressor";	
}

int NullAudioCompressor::compress(short* pcm_data, int sample_count, char* output_buffer, int output_buffer_size)
{
	return 0;
}

#endif
