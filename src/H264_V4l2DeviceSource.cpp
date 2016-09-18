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
H264_V4L2DeviceSource* H264_V4L2DeviceSource::createNew(UsageEnvironment& env, DeviceCapture * device, int outputFd, unsigned int queueSize, bool useThread, bool repeatConfig, bool keepMarker) 
{ 	
	H264_V4L2DeviceSource* source = NULL;
	if (device)
	{
		source = new H264_V4L2DeviceSource(env, device, outputFd, queueSize, useThread, repeatConfig, keepMarker);
	}
	return source;
}

// Constructor
H264_V4L2DeviceSource::H264_V4L2DeviceSource(UsageEnvironment& env, DeviceCapture * device, int outputFd, unsigned int queueSize, bool useThread, bool repeatConfig, bool keepMarker) 
	: V4L2DeviceSource(env, device, outputFd, queueSize, useThread), m_repeatConfig(repeatConfig), m_keepMarker(keepMarker), m_frameType(0)
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
	
	size_t bufSize = frameSize;
	size_t size = 0;
	unsigned char* buffer = this->extractFrame(frame, bufSize, size);
	while (buffer != NULL)				
	{
		switch (m_frameType)					
		{
			case 7: LOG(INFO) << "SPS size:" << size << " bufSize:" << bufSize; m_sps.assign((char*)buffer,size); break;
			case 8: LOG(INFO) << "PPS size:" << size << " bufSize:" << bufSize; m_pps.assign((char*)buffer,size); break;
			case 5: LOG(INFO) << "IDR size:" << size << " bufSize:" << bufSize; 
				if (m_repeatConfig && !m_sps.empty() && !m_pps.empty())
				{
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_sps.c_str(), m_sps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_pps.c_str(), m_pps.size()));
				}
			break;
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
			LOG(NOTICE) << m_auxLine;
			
			delete [] sps_base64;
			delete [] pps_base64;
		}
		frameList.push_back(std::pair<unsigned char*,size_t>(buffer, size));
		
		buffer = this->extractFrame(&buffer[size], bufSize, size);
	}
	return frameList;
}

// extract a frame
unsigned char*  H264_V4L2DeviceSource::extractFrame(unsigned char* frame, size_t& size, size_t& outsize)
{			
	unsigned char * outFrame = NULL;
	outsize = 0;
	unsigned int markerlength = 0;
	m_frameType = 0;	
	if ( (size>= sizeof(H264marker)) && (memcmp(frame,H264marker,sizeof(H264marker)) == 0) )
	{
		markerlength = sizeof(H264marker);
	}
	else if ( (size>= sizeof(H264shortmarker)) && (memcmp(frame,H264shortmarker,sizeof(H264shortmarker)) == 0) )
	{
		markerlength = sizeof(H264shortmarker);
	}
	
	if (markerlength != 0)
	{
		m_frameType = (frame[markerlength]&0x1F);
		unsigned char * ptr = (unsigned char*)memmem(&frame[markerlength], size-markerlength, H264marker, sizeof(H264marker));
		if (ptr == NULL)
		{
			 ptr = (unsigned char*)memmem(&frame[markerlength], size-markerlength, H264shortmarker, sizeof(H264shortmarker));
		}
		if (m_keepMarker)
		{
			outFrame = &frame[0];
		}
		else
		{
			size -=  markerlength;
			outFrame = &frame[markerlength];
		}
		if (ptr != NULL)
		{
			outsize = ptr - outFrame;
		}
		else
		{
			outsize = size;
		}
		size -= outsize;
	}
	return outFrame;
}

