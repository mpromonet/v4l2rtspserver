/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4L2MMAPDeviceSource.h
** 
** V4L2 source using mmap API
**
** -------------------------------------------------------------------------*/


#ifndef V4L2_MMAP_DEVICE_SOURCE
#define V4L2_MMAP_DEVICE_SOURCE

// project
#include "V4l2DeviceSource.h"

#define V4L2MMAP_NBBUFFER 10
class V4L2MMAPDeviceSource : public V4L2DeviceSource
{
	public:
		static V4L2MMAPDeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params);
	
	protected:
		V4L2MMAPDeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : V4L2DeviceSource(env, params), n_buffers(0) {};
			
	protected:
		virtual bool captureStart();
		virtual size_t read(char* buffer, size_t bufferSize);
		virtual bool captureStop();
	
	protected:
		int n_buffers;
	
		struct buffer 
		{
			void *                  start;
			size_t                  length;
		};
		buffer m_buffer[V4L2MMAP_NBBUFFER];
};

#endif