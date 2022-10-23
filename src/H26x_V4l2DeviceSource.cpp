/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H26x_V4l2DeviceSource.cpp
** 
** H264/H265 V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <sstream>

// live555
#include <Base64.hh>

// project
#include "logger.h"
#include "H26x_V4l2DeviceSource.h"

// extract a frame
unsigned char*  H26X_V4L2DeviceSource::extractFrame(unsigned char* frame, size_t& size, size_t& outsize, int& frameType)
{						
	unsigned char * outFrame = NULL;
	outsize = 0;
	unsigned int markerlength = 0;
	frameType = 0;
	
	unsigned char *startFrame = (unsigned char*)memmem(frame,size,H264marker,sizeof(H264marker));
	if (startFrame != NULL) {
		markerlength = sizeof(H264marker);
	} else {
		startFrame = (unsigned char*)memmem(frame,size,H264shortmarker,sizeof(H264shortmarker));
		if (startFrame != NULL) {
			markerlength = sizeof(H264shortmarker);
		}
	}
	if (startFrame != NULL) {
		frameType = startFrame[markerlength];
		
		int remainingSize = size-(startFrame-frame+markerlength);		
		unsigned char *endFrame = (unsigned char*)memmem(&startFrame[markerlength], remainingSize, H264marker, sizeof(H264marker));
		if (endFrame == NULL) {
			endFrame = (unsigned char*)memmem(&startFrame[markerlength], remainingSize, H264shortmarker, sizeof(H264shortmarker));
		}
		
		if (m_keepMarker)
		{
			size -=  startFrame-frame;
			outFrame = startFrame;
		}
		else
		{
			size -=  startFrame-frame+markerlength;
			outFrame = &startFrame[markerlength];
		}
		
		if (endFrame != NULL)
		{
			outsize = endFrame - outFrame;
		}
		else
		{
			outsize = size;
		}
		size -= outsize;		
	} else if (size>= sizeof(H264shortmarker)) {
		 LOG(INFO) << "No marker found";
	}

	return outFrame;
}

std::string H26X_V4L2DeviceSource::getFrameWithMarker(const std::string & frame) {
	std::string frameWithMarker;
	frameWithMarker.append(H264marker, sizeof(H264marker));
	frameWithMarker.append(frame);
	return frameWithMarker;
}

