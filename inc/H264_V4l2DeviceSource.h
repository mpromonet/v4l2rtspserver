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


#pragma once

// project
#include "V4L2DeviceSource.h"

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------
const char H264marker[] = {0,0,0,1};
const char H264shortmarker[] = {0,0,1};

class H26X_V4L2DeviceSource : public V4L2DeviceSource
{
	protected:
		H26X_V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker)
			: V4L2DeviceSource(env, device, outputFd, queueSize, captureMode), m_repeatConfig(repeatConfig), m_keepMarker(keepMarker) {}
				
		virtual ~H26X_V4L2DeviceSource() {}

		unsigned char* extractFrame(unsigned char* frame, size_t& size, size_t& outsize, int& frameType);
				
	protected:
		std::string m_sps;
		std::string m_pps;
		bool        m_repeatConfig;
		bool        m_keepMarker;
};

class H264_V4L2DeviceSource : public H26X_V4L2DeviceSource
{
	public:				
		static H264_V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) {
			return new H264_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker);
		}

	protected:
		H264_V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) 
			: H26X_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker) {} 
	
		// overide V4L2DeviceSource
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);			
};

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
				
	protected:
		std::string m_vps;
};

