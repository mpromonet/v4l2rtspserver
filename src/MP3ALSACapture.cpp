/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MP3ALSACapture.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** ALSA capture overide of V4l2Capture
**                                                                                    
** -------------------------------------------------------------------------*/

#ifdef HAVE_ALSA

#include "MP3ALSACapture.h"


MP3ALSACapture::MP3ALSACapture(const ALSACaptureParameters & params) : ALSACapture(params)
{
	LOG(NOTICE) << "Open MP3ALSACapture device";
	
}

#endif


