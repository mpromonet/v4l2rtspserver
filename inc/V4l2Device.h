/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2Device.h
** 
** V4L2 wrapper 
**
** -------------------------------------------------------------------------*/


#ifndef V4L2_DEVICE
#define V4L2_DEVICE

#include <string>
#include <list> 
#include <iostream>

// ---------------------------------
// V4L2 Capture parameters
// ---------------------------------
struct V4L2DeviceParameters 
{
	V4L2DeviceParameters(const char* devname, unsigned int format, unsigned int width, unsigned int height, int fps, int verbose) : 
		m_devName(devname), m_format(format), m_width(width), m_height(height), m_fps(fps), m_verbose(verbose) {};
		
	std::string m_devName;
	unsigned int m_format;
	unsigned int m_width;
	unsigned int m_height;
	int m_fps;			
	int m_verbose;
};

// ---------------------------------
// V4L2 Capture
// ---------------------------------
class V4L2Device
{		
	protected:
		V4L2Device(V4L2DeviceParameters params);
	
	public:
		virtual ~V4L2Device();
	
	public:
		int getBufferSize() { return m_bufferSize; };
		int getFd() { return m_fd; };		

	protected:
		bool init(unsigned int mandatoryCapabilities);
		int initdevice(const char *dev_name, unsigned int mandatoryCapabilities);
		int checkCapabilities(int fd, unsigned int mandatoryCapabilities);
		int configureFormat(int fd);
		int configureParam(int fd);		
		int xioctl(int fd, int request, void *arg);
				
	public:
		virtual bool captureStart() = 0;
		virtual size_t read(char* buffer, size_t bufferSize) = 0;
		virtual bool captureStop() = 0;
		
	protected:
		V4L2DeviceParameters m_params;
		int m_fd;
		int m_bufferSize;
};

#endif
