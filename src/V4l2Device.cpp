/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2Device.cpp
** 
** V4L2 wrapper 
**
** -------------------------------------------------------------------------*/

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <iomanip>
#include <sstream>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// project
#include "V4l2Device.h"

// Constructor
V4L2Device::V4L2Device(V4L2DeviceParameters params) : m_params(params), m_fd(-1), m_bufferSize(0)
{
}

// Destructor
V4L2Device::~V4L2Device()
{
	if (m_fd !=-1) v4l2_close(m_fd);
}

// intialize the source
bool V4L2Device::init(unsigned int mandatoryCapabilities)
{
	if (initdevice(m_params.m_devName.c_str(), mandatoryCapabilities) == -1)
	{
		fprintf(stderr, "Init device:%s failure\n", m_params.m_devName.c_str());
	}
	return (m_fd!=-1);
}
		
// intialize the V4L2 device
int V4L2Device::initdevice(const char *dev_name, unsigned int mandatoryCapabilities)
{
	m_fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
	if (m_fd < 0) 
	{
		perror("Cannot open device");
		return -1;
	}
	if (checkCapabilities(m_fd,mandatoryCapabilities) !=0)
	{
		return -1;
	}	
	if (configureFormat(m_fd) !=0)
	{
		return -1;
	}
	if (configureParam(m_fd) !=0)
	{
		return -1;
	}
	if (!this->captureStart())
	{
		return -1;
	}
	
	return m_fd;
}

// check needed V4L2 capabilities
int V4L2Device::checkCapabilities(int fd, unsigned int mandatoryCapabilities)
{
	struct v4l2_capability cap;
	memset(&(cap), 0, sizeof(cap));
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) 
	{
		fprintf(stderr, "xioctl cannot get capabilities error %d, %s\n", errno, strerror(errno));
		return -1;
	}
	fprintf(stderr, "driver:%s capabilities;%X\n", cap.driver, cap.capabilities);

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
	{
		fprintf(stderr, "%s is no video capture device\n", m_params.m_devName.c_str());
		return -1;
	}
	
	if ((cap.capabilities & V4L2_CAP_READWRITE)) fprintf(stderr, "%s support read i/o\n", m_params.m_devName.c_str());
	if ((cap.capabilities & V4L2_CAP_STREAMING))  fprintf(stderr, "%s support streaming i/o\n", m_params.m_devName.c_str());
	if ((cap.capabilities & V4L2_CAP_TIMEPERFRAME)) fprintf(stderr, "%s support timeperframe\n", m_params.m_devName.c_str());
	
	if ( (cap.capabilities & mandatoryCapabilities) != mandatoryCapabilities )
	{
		fprintf(stderr, "%s mandatory capabilities not available\n", m_params.m_devName.c_str());
		return -1;
	}
	
	return 0;
}

// configure capture format 
int V4L2Device::configureFormat(int fd)
{
	struct v4l2_format   fmt;			
	memset(&(fmt), 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = m_params.m_width;
	fmt.fmt.pix.height      = m_params.m_height;
	fmt.fmt.pix.pixelformat = m_params.m_format;
	fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
	{
		fprintf(stderr, "xioctl cannot set format error %d, %s\n", errno, strerror(errno));
		return -1;
	}			
	if (fmt.fmt.pix.pixelformat != m_params.m_format) 
	{
		printf("Libv4l didn't accept format (%d). Can't proceed.\n", m_params.m_format);
		return -1;
	}
	if ((fmt.fmt.pix.width != m_params.m_width) || (fmt.fmt.pix.height != m_params.m_height))
	{
		printf("Warning: driver is sending image at %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.width);
	}
	
	m_bufferSize =  fmt.fmt.pix.sizeimage;
	return 0;
}

// configure capture FPS 
int V4L2Device::configureParam(int fd)
{
	struct v4l2_streamparm   param;			
	memset(&(param), 0, sizeof(param));
	param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	param.parm.capture.timeperframe.numerator = 1;
	param.parm.capture.timeperframe.denominator = m_params.m_fps;

	if (xioctl(fd, VIDIOC_S_PARM, &param) == -1)
	{
		fprintf(stderr, "xioctl cannot set param error %d, %s\n", errno, strerror(errno));
		return -1;
	}
	
	fprintf(stderr, "fps :%d/%d nbBuffer:%d\n", param.parm.capture.timeperframe.numerator, param.parm.capture.timeperframe.denominator, param.parm.capture.readbuffers);
	
	return 0;
}

// ioctl encapsulation
int V4L2Device::xioctl(int fd, int request, void *arg)
{
	int ret = -1;
	do 
	{
		ret = v4l2_ioctl(fd, request, arg);
	} while (ret == -1 && ((errno == EINTR) || (errno == EAGAIN)));

	return ret;
}
				

