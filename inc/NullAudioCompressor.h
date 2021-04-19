/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
** -------------------------------------------------------------------------*/

#pragma once

#include "logger.h"
#include "AudioCompressor.h"

class NullAudioCompressor: public AudioCompressor
{
	public:
		NullAudioCompressor();
		int compress(short* pcm_data, int sample_count, char* output_buffer, int output_buffer_size);		
	private:
};
