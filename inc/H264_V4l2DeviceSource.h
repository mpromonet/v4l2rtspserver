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

#include <vector>

// project
#include "H26x_V4l2DeviceSource.h"
#include "SnapshotManager.h"

class QuickTimeMuxer; // Forward declaration

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------
// Note: H264marker and H264shortmarker are defined in H26x_V4l2DeviceSource.h

class H264_V4L2DeviceSource : public H26X_V4L2DeviceSource
{
	public:				
		static H264_V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker, bool isMP4 = false) {
			return new H264_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker, isMP4);
		}

	protected:
		H264_V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker, bool isMP4 = false) 
			: H26X_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker), m_quickTimeMuxer(nullptr), m_isMP4(isMP4), m_currentFrameData(), m_currentFrameIsKeyframe(false) {
			// Check if output file is MP4 based on file descriptor (simple heuristic)
			// This could be improved by passing a flag from the caller
		}

		virtual ~H264_V4L2DeviceSource();
	
		// overide V4L2DeviceSource
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);
		virtual std::list< std::string > getInitFrames();
		virtual bool isKeyFrame(const char*, int);
		
	private:
		QuickTimeMuxer* m_quickTimeMuxer;
		bool m_isMP4;
		std::vector<unsigned char> m_currentFrameData;
		bool m_currentFrameIsKeyframe;
		void initQuickTimeMuxerIfNeeded();
};
