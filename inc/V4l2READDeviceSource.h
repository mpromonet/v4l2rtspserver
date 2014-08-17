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
#include "V4l2DeviceSource.h"

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
class V4L2READDeviceSource : public V4L2DeviceSource 
{
	public:
		static V4L2READDeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params);
	
	protected:
		V4L2READDeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : V4L2DeviceSource(env, params) {};
			
	protected:
		virtual bool captureStart() { return true; };
		virtual size_t read(char* buffer, size_t bufferSize);
		virtual bool captureStop() { return true; };
};

#endif