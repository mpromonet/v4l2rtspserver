/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MP3AudioCompressor.h
** 
** Contains abstract class for audio compressors
**
** -------------------------------------------------------------------------*/

#pragma once

#include "logger.h"
#include <lame.h>
#include "AudioCompressor.h"

class MP3AudioCompressor: public AudioCompressor
{
	public:
		MP3AudioCompressor();
		MP3AudioCompressor(int channels, int samplerate);
		int compress(short* pcm_data, int sample_count, char* output_buffer, int output_buffer_size);		
	private:
		lame_global_flags* gfp;
};
