/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2Capture.cpp
** 
** V4L2 wrapper 
**
** -------------------------------------------------------------------------*/

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <sys/ioctl.h>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// project
#include "V4l2Capture.h"

// Constructor
V4l2Capture::V4l2Capture(V4L2DeviceParameters params) : m_params(params), m_fd(-1), m_bufferSize(0), m_format(0)
{
}

// Destructor
V4l2Capture::~V4l2Capture()
{
	if (m_fd !=-1) v4l2_close(m_fd);
}

// intialize the V4L2 connection
bool V4l2Capture::init(unsigned int mandatoryCapabilities)
{
	if (initdevice(m_params.m_devName.c_str(), mandatoryCapabilities) == -1)
	{
		fprintf(stderr, "[%s] Init device:%s failure\n", __FILE__, m_params.m_devName.c_str());

	}
	return (m_fd!=-1);
}

// close the V4L2 connection
void V4l2Capture::close()
{
	if (m_fd != -1) v4l2_close(m_fd);
	m_fd = -1;
}

// intialize the V4L2 device
int V4l2Capture::initdevice(const char *dev_name, unsigned int mandatoryCapabilities)
{
	m_fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
	if (m_fd < 0) 
	{
		perror("Cannot open device");
		this->close();
		return -1;
	}
	if (checkCapabilities(m_fd,mandatoryCapabilities) !=0)
	{
		this->close();
		return -1;
	}	
	if (configureFormat(m_fd) !=0)
	{
		this->close();
		return -1;
	}
	if (configureParam(m_fd) !=0)
	{
		this->close();
		return -1;
	}
	
	return m_fd;
}

// check needed V4L2 capabilities
int V4l2Capture::checkCapabilities(int fd, unsigned int mandatoryCapabilities)
{
	struct v4l2_capability cap;
	memset(&(cap), 0, sizeof(cap));
	if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) 
	{
		fprintf(stderr, "[%s] xioctl cannot get capabilities error %d, %s\n", __FILE__, errno, strerror(errno));
		return -1;
	}
	fprintf(stderr, "driver:%s capabilities;%X\n", cap.driver, cap.capabilities);

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
	{
		fprintf(stderr, "[%s] the device '%s' doesnot support capture\n", __FILE__, m_params.m_devName.c_str());
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
int V4l2Capture::configureFormat(int fd)
{
	struct v4l2_format   fmt;			
	memset(&(fmt), 0, sizeof(fmt));
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = m_params.m_width;
	fmt.fmt.pix.height      = m_params.m_height;
	fmt.fmt.pix.pixelformat = m_params.m_format;
	fmt.fmt.pix.field       = V4L2_FIELD_ANY;
	
	if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1)
	{
		fprintf(stderr, "Cannot set format error %d, %s\n", errno, strerror(errno));
		return -1;
	}			
	if (fmt.fmt.pix.pixelformat != m_params.m_format) 
	{
		printf("Error: format (%d) refused.\n", m_params.m_format);
		return -1;
	}
	if ((fmt.fmt.pix.width != m_params.m_width) || (fmt.fmt.pix.height != m_params.m_height))
	{
		printf("Warning: driver is sending image at %dx%d\n", fmt.fmt.pix.width, fmt.fmt.pix.width);
	}
	
	m_format     = fmt.fmt.pix.pixelformat;
	m_bufferSize = fmt.fmt.pix.sizeimage;
	
	fprintf(stderr, "[%s] bufferSize:%d\n", __FILE__, m_bufferSize);
	
	return 0;
}

// configure capture FPS 
int V4l2Capture::configureParam(int fd)
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


// query current format
void V4l2Capture::queryFormat()
{
	struct v4l2_format     fmt;
	memset(&fmt,0,sizeof(fmt));
	fmt.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (0 == ioctl(m_fd,VIDIOC_G_FMT,&fmt)) // don't understand why xioctl give a different result
	{
		m_format     = fmt.fmt.pix.pixelformat;
		m_bufferSize = fmt.fmt.pix.sizeimage;
	}
}

// ioctl encapsulation
int V4l2Capture::xioctl(int fd, int request, void *arg)
{
	int ret = -1;
	errno=0;
	do 
	{
		ret = v4l2_ioctl(fd, request, arg);
	} while ((ret == -1) && ((errno == EINTR) || (errno == EAGAIN)));

	return ret;
}
				

