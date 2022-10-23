/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H265_V4l2DeviceSource.h
** 
** H265 V4L2 live555 source 
**
** -------------------------------------------------------------------------*/


#pragma once

// project
#include "H26x_V4l2DeviceSource.h"

class H265_V4L2DeviceSource : public H26X_V4L2DeviceSource
{
	public:				
		static H265_V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) {
			return new H265_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker);
		}

	protected:
		H265_V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) 
			: H26X_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker) {} 
	
		// overide V4L2DeviceSource
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);			
		virtual std::list< std::string > getInitFrames();
				
	protected:
		std::string m_vps;
};

