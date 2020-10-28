/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MP3ALSACapture.h
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** This class allows reading from an ALSA device and encoding it as MP3 audio
** using LAME.                     
**                                                                                    
** -------------------------------------------------------------------------*/

#pragma once

#include "logger.h"
#include "ALSACapture.h"

class MP3ALSACapture : public ALSACapture
{
	public:
		MP3ALSACapture(const ALSACaptureParameters & params);
			
	private:
};
