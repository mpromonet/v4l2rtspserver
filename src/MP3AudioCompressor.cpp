/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
**                                                                                    
** -------------------------------------------------------------------------*/

#ifdef HAVE_ALSA

#include "MP3AudioCompressor.h"



/**
 * lame_error_callback, lame_message_callback, lame_debug_callback: LAME
 * logging callback functions.
 *
 * [Parameters]
 *     format: Format string.
 *     args: Format arguments.
 */

static void lame_error_callback(const char *format, va_list args)
{ 
       char buff[512];
       vsnprintf(buff, sizeof(buff), format, args);
       LOG(ERROR) << buff;
}

static void lame_message_callback(const char *format, va_list args)
{
       char buff[512];
       vsnprintf(buff, sizeof(buff), format, args);
       LOG(NOTICE) << buff;
}

MP3AudioCompressor::MP3AudioCompressor(void)
{
	int err;

	LOG(NOTICE) << "Initializing MP3AudioCompressor";	

	// Lame Init
	gfp = lame_init();

	lame_set_errorf(gfp, lame_error_callback);
	lame_set_msgf(gfp, lame_message_callback);
	//lame_set_debugf(lame, lame_debug_callback);
	lame_set_num_channels(gfp, 1);
	//lame_set_mode(gfp, 3);
	lame_set_in_samplerate(gfp, 48000);
	lame_set_out_samplerate(gfp, 48000);
	//  lame_set_scale(gfp, 3.0);
	err = lame_init_params(gfp);

	if (err < 0) {
	   LOG(ERROR) << "Error initializing Lame encoder";
	}
	lame_print_config(gfp);
}

MP3AudioCompressor::MP3AudioCompressor(int channels, int samplerate)
{
	int err;

	LOG(NOTICE) << "Initializing MP3AudioCompressor";	

	// Lame Init
	gfp = lame_init();

	lame_set_errorf(gfp, lame_error_callback);
	lame_set_msgf(gfp, lame_message_callback);
	//lame_set_debugf(lame, lame_debug_callback);
	lame_set_num_channels(gfp, channels);
	//lame_set_mode(gfp, 3);
	lame_set_in_samplerate(gfp, samplerate);
	lame_set_out_samplerate(gfp, samplerate);
	//  lame_set_scale(gfp, 3.0);
	err = lame_init_params(gfp);

	if (err < 0) {
	   LOG(ERROR) << "Error initializing Lame encoder";
	}
	lame_print_config(gfp);
}


int MP3AudioCompressor::compress(short* pcm_data, int sample_count, char* output_buffer, int output_buffer_size)
{
	int bytescopied = 0;


	// Encode the buffer. Even though we use snd_pcm_readi
	// for interleaved frames when there is only one channel you
	// just get an array of data that has no interleaved. 
	bytescopied = lame_encode_buffer(
		gfp,
		(short int *)pcm_data, 				// Left raw PCM data
		NULL,								// Right raw PCM data
		sample_count,     			   	    // Sample count
		(unsigned char*)output_buffer,      // Output buffer
		output_buffer_size                  // Outbut buffer size
	);		

	LOG(DEBUG) << "Encoded " << bytescopied << " bytes to mp3";

	return bytescopied;

}



#endif

