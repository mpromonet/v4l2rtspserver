/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264_V4l2DeviceSource.h
** 
** H264 V4L2 live555 source 
**
** -------------------------------------------------------------------------*/


#ifndef H264_V4L2_DEVICE_SOURCE
#define H264_V4L2_DEVICE_SOURCE

// project
#include "V4l2DeviceSource.h"

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------
const char H264marker[] = {0,0,0,1};
class H264_V4L2DeviceSource : public V4L2DeviceSource
{
	public:				
		static H264_V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, bool useThread) ;

	protected:
		H264_V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, bool useThread);
		virtual ~H264_V4L2DeviceSource();

		unsigned char* extractFrame(unsigned char* frame, size_t& size, size_t& outsize);
	
		// overide V4L2DeviceSource
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);
			
	private:
		std::string m_sps;
		std::string m_pps;
};

#endif
