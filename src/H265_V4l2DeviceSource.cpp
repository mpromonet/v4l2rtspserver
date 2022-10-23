/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H265_V4l2DeviceSource.cpp
** 
** H265 V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <sstream>

// live555
#include <Base64.hh>

// project
#include "logger.h"
#include "H265_V4l2DeviceSource.h"

// split packet in frames					
std::list< std::pair<unsigned char*,size_t> > H265_V4L2DeviceSource::splitFrames(unsigned char* frame, unsigned frameSize) 
{				
	std::list< std::pair<unsigned char*,size_t> > frameList;
	
	size_t bufSize = frameSize;
	size_t size = 0;
	int frameType = 0;
	unsigned char* buffer = this->extractFrame(frame, bufSize, size, frameType);
	while (buffer != NULL)				
	{
		switch ((frameType&0x7E)>>1)					
		{
			case 32: LOG(INFO) << "VPS size:" << size << " bufSize:" << bufSize; m_vps.assign((char*)buffer,size); break;
			case 33: LOG(INFO) << "SPS size:" << size << " bufSize:" << bufSize; m_sps.assign((char*)buffer,size); break;
			case 34: LOG(INFO) << "PPS size:" << size << " bufSize:" << bufSize; m_pps.assign((char*)buffer,size); break;
			case 19: 
			case 20: LOG(INFO) << "IDR size:" << size << " bufSize:" << bufSize; 
				if (m_repeatConfig && !m_vps.empty() && !m_sps.empty() && !m_pps.empty())
				{
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_vps.c_str(), m_vps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_sps.c_str(), m_sps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_pps.c_str(), m_pps.size()));
				}
			break;
			default: break;
		}
		
		if (!m_vps.empty() && !m_sps.empty() && !m_pps.empty())
		{		
			char* vps_base64 = base64Encode(m_vps.c_str(), m_vps.size());
			char* sps_base64 = base64Encode(m_sps.c_str(), m_sps.size());
			char* pps_base64 = base64Encode(m_pps.c_str(), m_pps.size());		

			std::ostringstream os; 
			os << "sprop-vps=" << vps_base64;
			os << ";sprop-sps=" << sps_base64;
			os << ";sprop-pps=" << pps_base64;
			m_auxLine.assign(os.str());
			
			delete [] vps_base64;
			delete [] sps_base64;
			delete [] pps_base64;
		}
		frameList.push_back(std::pair<unsigned char*,size_t>(buffer, size));
		
		buffer = this->extractFrame(&buffer[size], bufSize, size, frameType);
	}
	return frameList;
}

std::list< std::string > H265_V4L2DeviceSource::getInitFrames() {
	std::list< std::string > frameList;
	frameList.push_back(this->getFrameWithMarker(m_vps));
	frameList.push_back(this->getFrameWithMarker(m_sps));
	frameList.push_back(this->getFrameWithMarker(m_pps));
	return frameList;
}
