/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2READDeviceSource.h
** 
** V4L2 source using read API
**
** -------------------------------------------------------------------------*/


#ifndef V4L2_READ_DEVICE_SOURCE
#define V4L2_READ_DEVICE_SOURCE

// project
#include "V4l2Device.h"

class V4L2READDeviceSource : public V4L2Device
{
	public:
		static V4L2READDeviceSource* createNew(V4L2DeviceParameters params);
	
	protected:
		V4L2READDeviceSource(V4L2DeviceParameters params) : V4L2Device(params) {};
			
	public:
		virtual bool captureStart() { return true; };
		virtual size_t read(char* buffer, size_t bufferSize);
		virtual bool captureStop() { return true; };
};

#endif