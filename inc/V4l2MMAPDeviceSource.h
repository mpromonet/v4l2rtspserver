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
#include "V4l2Device.h"

#define V4L2MMAP_NBBUFFER 10
class V4L2MMAPDeviceSource : public V4L2Device
{
	public:
		static V4L2MMAPDeviceSource* createNew(V4L2DeviceParameters params);
	
	protected:
		V4L2MMAPDeviceSource(V4L2DeviceParameters params) : V4L2Device(params), n_buffers(0) {};
			
	public:
		virtual bool captureStart();
		virtual size_t read(char* buffer, size_t bufferSize);
		virtual bool captureStop();
	
	protected:
		unsigned int n_buffers;
	
		struct buffer 
		{
			void *                  start;
			size_t                  length;
		};
		buffer m_buffer[V4L2MMAP_NBBUFFER];
};

#endif

