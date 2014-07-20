/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2DeviceSource.cpp
** 
** V4L2 source 
**
** -------------------------------------------------------------------------*/

#include <fcntl.h>

#include <iomanip>
#include <sstream>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <Base64.hh>

// project
#include "V4l2DeviceSource.h"

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------

// Creator
V4L2DeviceSource* V4L2DeviceSource::createNew(UsageEnvironment& env, V4L2DeviceParameters params) 
{ 
	V4L2DeviceSource* device = new V4L2DeviceSource(env, params); 
	if (device && !device->init())
	{
		delete device;
		device=NULL;
	}
	return device;
}

// Constructor
V4L2DeviceSource::V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params) : FramedSource(env), m_params(params), m_fd(-1), m_bufferSize(0), m_in("in"), m_out("out") , m_outfile(NULL)
{
	m_eventTriggerId = envir().taskScheduler().createEventTrigger(V4L2DeviceSource::deliverFrameStub);
}

// Destructor
V4L2DeviceSource::~V4L2DeviceSource()
{
	envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
	v4l2_close(m_fd);
	if (m_outfile) fclose(m_outfile);
}

// intialize the source
bool V4L2DeviceSource::init()
{
	m_fd = initdevice(m_params.m_devName.c_str());
	if (m_fd == -1)
	{
		fprintf(stderr, "Init device:%s failure\n", m_params.m_devName.c_str());
	}
	else
	{
		envir().taskScheduler().turnOnBackgroundReadHandling( m_fd, V4L2DeviceSource::incomingPacketHandlerStub,this);
	}
	return (m_fd!=-1);
}
		
// intialize the V4L2 device
int V4L2DeviceSource::initdevice(const char *dev_name)
{
	int fd = v4l2_open(dev_name, O_RDWR | O_NONBLOCK, 0);
	if (fd < 0) 
	{
		perror("Cannot open device");
		return -1;
	}
	
	if (checkCapabilities(fd) !=0)
	{
		return -1;
	}
	
	if (configureFormat(fd) !=0)
	{
		return -1;
	}

	if (configureParam(fd) !=0)
	{
		return -1;
	}
	if (!m_params.m_outputFIle.empty())
	{
		fprintf(stderr, "OutputFile:%s\n", m_params.m_outputFIle.c_str());
		m_outfile = fopen(m_params.m_outputFIle.c_str(),"w");
	}
	
	return fd;
}

// check needed V4L2 capabilities
int V4L2DeviceSource::checkCapabilities(int fd)
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
	
	return 0;
}

// configure capture format 
int V4L2DeviceSource::configureFormat(int fd)
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
int V4L2DeviceSource::configureParam(int fd)
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
int V4L2DeviceSource::xioctl(int fd, int request, void *arg)
{
	int r = -1;

	do 
	{
		r = v4l2_ioctl(fd, request, arg);
	} while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

	return r;
}
				
// FrameSource callback
void V4L2DeviceSource::doGetNextFrame()
{
	if (!m_captureQueue.empty())
	{
		deliverFrame();
	}
}
		
void V4L2DeviceSource::deliverFrame()
{			
	if (isCurrentlyAwaitingData()) 
	{
		fDurationInMicroseconds = 0;
		fFrameSize = 0;
		
		if (m_captureQueue.empty())
		{
			if ( m_params.m_verbose) envir() << "Queue is empty \n";		
		}
		else
		{				
			gettimeofday(&fPresentationTime, NULL);			
			m_out.notify(fPresentationTime.tv_sec);
			
			Frame * frame = m_captureQueue.front();
			m_captureQueue.pop_front();
											
			if (frame->m_size > fMaxSize) 
			{
				fFrameSize = fMaxSize;
				fNumTruncatedBytes = frame->m_size - fMaxSize;
			} 
			else 
			{
				fFrameSize = frame->m_size;
			}
			timeval diff;
			timersub(&fPresentationTime,&(frame->m_timestamp),&diff);
			
			if (m_params.m_verbose) 
			{
				printf ("deliverFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d\n",fPresentationTime.tv_sec, fPresentationTime.tv_usec, fFrameSize,  (int)(diff.tv_sec*1000+diff.tv_usec/1000),  m_captureQueue.size());
			}
			
			memcpy(fTo, frame->m_buffer, fFrameSize);
			delete frame;
		}
		
		// send Frame to the consumer
		FramedSource::afterGetting(this);			
	}
}
			
// FrameSource callback on read event
void V4L2DeviceSource::getNextFrame() 
{
	char* buffer = new char[m_bufferSize];
	
	timeval ref;
	gettimeofday(&ref, NULL);											
	int frameSize = v4l2_read(m_fd, buffer,  m_bufferSize);
	
	if (frameSize < 0)
	{
		envir() << "V4L2DeviceSource::getNextFrame fd:"  << m_fd << " errno:" << errno << " "  << strerror(errno) << "\n";		
		delete buffer;
		handleClosure(this);
	}
	else
	{
		timeval tv;
		gettimeofday(&tv, NULL);												
		timeval diff;
		timersub(&tv,&ref,&diff);
		int fps = m_in.notify(tv.tv_sec);
		if (m_params.m_verbose) 
		{
			printf ("getNextFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d\n", ref.tv_sec, ref.tv_usec, frameSize, (int)(diff.tv_sec*1000+diff.tv_usec/1000), m_captureQueue.size());
		}
		processFrame(buffer,frameSize,ref);
		if (!processConfigrationFrame(buffer,frameSize))
		{
			queueFrame(buffer,frameSize,ref);
		}
	}			
}	

bool V4L2DeviceSource::processConfigrationFrame(char * frame, int frameSize) 
{
	bool ret = false;
	
	// save SPS and PPS
	u_int8_t nal_unit_type = frame[0]&0x1F;				
	if (nal_unit_type == 7)
	{
		std::cout << "SPS\n";	
		for (int i=0; i < frameSize; ++i)
		{
			if (memcmp(&frame[i],marker,sizeof(marker)) == 0)
			{
				size_t spsSize = i ;
				std::cout << "PPS" << "\n";	
				char* sps = (char*)memcpy(new char [spsSize], frame, spsSize);
				size_t ppsSize = frameSize - spsSize - sizeof(marker);
				char* pps = (char*)memcpy(new char [ppsSize], &frame[spsSize+sizeof(marker)], ppsSize);
										
				u_int32_t profile_level_id = 0;
				if (spsSize >= 4) 
				{
					profile_level_id = (sps[1]<<16)|(sps[2]<<8)|sps[3]; 
				}
				
				char* sps_base64 = base64Encode(sps, spsSize);
				char* pps_base64 = base64Encode(pps, ppsSize);		

				std::ostringstream os; 
				os << "profile-level-id=" << std::hex << std::setw(6) << profile_level_id;
				os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
				m_auxLine.assign(os.str());
				
				free(sps);
				free(pps);
				free(sps_base64);
				free(pps_base64);
				
				std::cout << "AuxLine:"  << m_auxLine << " \n";		
				ret = true;
				break;
			}						
		}
		delete [] frame;
	}
	return ret;
}
		
void V4L2DeviceSource::processFrame(char * frame, int &frameSize, const timeval &ref) 
{
	timeval tv;
	gettimeofday(&tv, NULL);												
	timeval diff;
	timersub(&tv,&ref,&diff);
	
	if (m_params.m_verbose) 
	{
		printf ("queueFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d data:%02X%02X%02X%02X%02X...\n", ref.tv_sec, ref.tv_usec, frameSize, (int)(diff.tv_sec*1000+diff.tv_usec/1000), m_captureQueue.size(), frame[0], frame[1], frame[2], frame[3], frame[4]);
	}
	if (m_outfile) fwrite(frame, frameSize,1, m_outfile);

	// remove marker
	if (memcmp(frame,marker,sizeof(marker)) == 0)
	{
		frameSize -= sizeof(marker);
		memmove(frame, &frame[sizeof(marker)], frameSize);
	}
}	
		
void V4L2DeviceSource::queueFrame(char * frame, int frameSize, const timeval &tv) 
{
	while (m_captureQueue.size() >= m_params.m_queueSize)
	{
		if (m_params.m_verbose) 
		{
			envir() << "Queue full size drop frame size:"  << (int)m_captureQueue.size() << " \n";		
		}
		delete m_captureQueue.front();
		m_captureQueue.pop_front();
	}
	m_captureQueue.push_back(new Frame(frame, frameSize,tv));	
	
	// post an event to ask to deliver the frame
	envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
}	

