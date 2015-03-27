/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264_V4l2DeviceSource.cpp
** 
** H264 V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <sstream>

// live555
#include <Base64.hh>

// project
#include "logger.h"
#include "H264_V4l2DeviceSource.h"

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------
H264_V4L2DeviceSource* H264_V4L2DeviceSource::createNew(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, bool useThread) 
{ 	
	H264_V4L2DeviceSource* source = NULL;
	if (device)
	{
		source = new H264_V4L2DeviceSource(env, params, device, outputFd, queueSize, useThread);
	}
	return source;
}

// Constructor
H264_V4L2DeviceSource::H264_V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, bool useThread) 
	: V4L2DeviceSource(env, params, device, outputFd, queueSize,useThread)
{
}

// Destructor
H264_V4L2DeviceSource::~H264_V4L2DeviceSource()
{	
}

// split packet in frames					
std::list< std::pair<unsigned char*,size_t> > H264_V4L2DeviceSource::splitFrames(unsigned char* frame, unsigned frameSize) 
{				
	std::list< std::pair<unsigned char*,size_t> > frameList;
	
	size_t size = 0;
	unsigned char* buffer = this->extractFrame(frame, frameSize, size);
	while (buffer != NULL)				
	{
		frameList.push_back(std::make_pair<unsigned char*,size_t>(buffer, size));
		switch (buffer[0]&0x1F)					
		{
			case 7: LOG(INFO) << "SPS size:" << size; m_sps.assign((char*)buffer,size); break;
			case 8: LOG(INFO) << "PPS size:" << size; m_pps.assign((char*)buffer,size); break;
			default: break;
		}
		
		if (m_auxLine.empty() && !m_sps.empty() && !m_pps.empty())
		{
			u_int32_t profile_level_id = 0;					
			if (m_sps.size() >= 4) profile_level_id = (m_sps[1]<<16)|(m_sps[2]<<8)|m_sps[3]; 
		
			char* sps_base64 = base64Encode(m_sps.c_str(), m_sps.size());
			char* pps_base64 = base64Encode(m_pps.c_str(), m_pps.size());		

			std::ostringstream os; 
			os << "profile-level-id=" << std::hex << std::setw(6) << profile_level_id;
			os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
			m_auxLine.assign(os.str());
			
			free(sps_base64);
			free(pps_base64);
			LOG(NOTICE) << m_auxLine;
		}
		
		frameSize -= size+sizeof(H264marker);				
		buffer = this->extractFrame(&buffer[size], frameSize, size);
	}
	return frameList;
}

// extract a frame
unsigned char*  H264_V4L2DeviceSource::extractFrame(unsigned char* frame, size_t size, size_t& outsize)
{			
	unsigned char * outFrame = NULL;
	outsize = 0;
	if ( (size>= sizeof(H264marker)) && (memcmp(frame,H264marker,sizeof(H264marker)) == 0) )
	{
		outFrame = &frame[sizeof(H264marker)];
		outsize = size - sizeof(H264marker);
		for (int i=0; i+sizeof(H264marker) < size; ++i)
		{
			if (memcmp(&outFrame[i],H264marker,sizeof(H264marker)) == 0)
			{
				outsize = i;
				break;
			}
		}
	}
	return outFrame;
}

