/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** AudioCompressor.h
** 
** Contains abstract class for audio compressors
**
** -------------------------------------------------------------------------*/

#pragma once

#include "logger.h"

class AudioCompressor
{
	public:
		AudioCompressor();
		virtual int compress(short* pcm_data, int sample_count, char* output_buffer, int output_buffer_size) { return -1; };
	private:
};
