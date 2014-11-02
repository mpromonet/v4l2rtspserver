/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2ReadCapture.cpp
** 
** V4L2 source using read API
**
** -------------------------------------------------------------------------*/


// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// project
#include "V4l2ReadCapture.h"

// Creator
V4l2ReadCapture* V4l2ReadCapture::createNew(V4L2DeviceParameters params) 
{ 
	V4l2ReadCapture* device = new V4l2ReadCapture(params); 
	if (device && !device->init(V4L2_CAP_READWRITE))
	{
		delete device;
		device=NULL;
	}
	return device;
}

size_t V4l2ReadCapture::read(char* buffer, size_t bufferSize)
{
	return v4l2_read(m_fd, buffer,  bufferSize);
}


