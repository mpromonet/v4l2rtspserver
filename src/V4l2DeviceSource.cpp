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
// V4L2 FramedSource Stats
// ---------------------------------
int  V4L2DeviceSource::Stats::notify(int tv_sec, int framesize, int verbose)
{
	m_fps++;
	m_size+=framesize;
	if (tv_sec != m_fps_sec)
	{
		if (verbose >= 1)
		{
			std::cout << m_msg  << "tv_sec:" <<   tv_sec << " fps:" << m_fps << " bandwidth:"<< (m_size/128) << "kbps\n";		
		}
		m_fps_sec = tv_sec;
		m_fps = 0;
		m_size = 0;
	}
	return m_fps;
}

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
V4L2DeviceSource* V4L2DeviceSource::createNew(UsageEnvironment& env, V4L2DeviceParameters params, V4L2Device * device, const std::string &outputFIle, int queueSize, int verbose) 
{ 	
	V4L2DeviceSource* source = NULL;
	if (device)
	{
		source = new V4L2DeviceSource(env, params, device, outputFIle, queueSize, verbose);
	}
	return source;
}

// Constructor
V4L2DeviceSource::V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params, V4L2Device * device, const std::string &outputFIle, int queueSize, int verbose) 
	: FramedSource(env), 
	m_params(params), 
	m_in("in"), 
	m_out("out") , 
	m_outfile(NULL), 
	m_device(device),
	m_outputFIle(outputFIle),
	m_queueSize(queueSize),
	m_verbose(verbose)
{
	m_eventTriggerId = envir().taskScheduler().createEventTrigger(V4L2DeviceSource::deliverFrameStub);
	if (m_device)
	{
		envir().taskScheduler().turnOnBackgroundReadHandling( m_device->getFd(), V4L2DeviceSource::incomingPacketHandlerStub, this);
	}
	if (!m_outputFIle.empty())
	{
		fprintf(stderr, "OutputFile:%s\n", m_outputFIle.c_str());
		m_outfile = fopen(m_outputFIle.c_str(),"w");
	}
}

// Destructor
V4L2DeviceSource::~V4L2DeviceSource()
{
	envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
	if (m_outfile) fclose(m_outfile);
	m_device->captureStop();
}
				
// getting FrameSource callback
void V4L2DeviceSource::doGetNextFrame()
{
	if (!m_captureQueue.empty())
	{
		deliverFrame();
	}
}

// stopping FrameSource callback
void V4L2DeviceSource::doStopGettingFrames()
{
	envir() << "V4L2DeviceSource::doStopGettingFrames\n";	
	FramedSource::doStopGettingFrames();
}

// deliver frame to the sink
void V4L2DeviceSource::deliverFrame()
{			
	if (isCurrentlyAwaitingData()) 
	{
		fDurationInMicroseconds = 0;
		fFrameSize = 0;
		
		if (m_captureQueue.empty())
		{
			if (m_verbose >= 2) envir() << "Queue is empty \n";		
		}
		else
		{				
			gettimeofday(&fPresentationTime, NULL);			
			Frame * frame = m_captureQueue.front();
			m_captureQueue.pop_front();
	
			m_out.notify(fPresentationTime.tv_sec, frame->m_size, m_verbose);
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
			
			if (m_verbose >= 2) 
			{
				printf ("deliverFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d\n",fPresentationTime.tv_sec, fPresentationTime.tv_usec, fFrameSize,  (int)(diff.tv_sec*1000+diff.tv_usec/1000),  m_captureQueue.size());
			}
			
			int offset = 0;
			if ( (fFrameSize>sizeof(marker)) && (memcmp(frame->m_buffer,marker,sizeof(marker)) == 0) )
			{
				offset = sizeof(marker);
			}
			fFrameSize -= offset;
			memcpy(fTo, frame->m_buffer+offset, fFrameSize);
			delete frame;
		}
		
		// send Frame to the consumer
		FramedSource::afterGetting(this);			
	}
}

// FrameSource callback on read event
void V4L2DeviceSource::getNextFrame() 
{
	char* buffer = new char[m_device->getBufferSize()];	
	timeval ref;
	gettimeofday(&ref, NULL);											
	int frameSize = m_device->read(buffer,  m_device->getBufferSize());
	
	if (frameSize < 0)
	{
		envir() << "V4L2DeviceSource::getNextFrame errno:" << errno << " "  << strerror(errno) << "\n";		
		delete buffer;
		handleClosure(this);
	}
	else if (frameSize == 0)
	{
		envir() << "V4L2DeviceSource::getNextFrame no data errno:" << errno << " "  << strerror(errno) << "\n";		
		delete buffer;
	}
	else
	{
		timeval tv;
		gettimeofday(&tv, NULL);												
		timeval diff;
		timersub(&tv,&ref,&diff);
		m_in.notify(tv.tv_sec, frameSize, m_verbose);
		if (m_verbose >=2) 
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
	
	if (memcmp(frame,marker,sizeof(marker)) == 0)
	{
		// save SPS and PPS
		ssize_t spsSize = -1;
		ssize_t ppsSize = -1;
		
		u_int8_t nal_unit_type = frame[sizeof(marker)]&0x1F;					
		if (nal_unit_type == 7)
		{
			std::cout << "SPS\n";	
			for (int i=sizeof(marker); i+sizeof(marker) < frameSize; ++i)
			{
				if (memcmp(&frame[i],marker,sizeof(marker)) == 0)
				{
					spsSize = i-sizeof(marker) ;
					std::cout << "SPS size:" << spsSize << "\n";					
		
					nal_unit_type = frame[i+sizeof(marker)]&0x1F;
					if (nal_unit_type == 8)
					{
						std::cout << "PPS\n";					
						for (int j=i+sizeof(marker); j+sizeof(marker) < frameSize; ++j)
						{
							if (memcmp(&frame[j],marker,sizeof(marker)) == 0)
							{
								ppsSize = j-sizeof(marker);
								std::cout << "PPS size:" << ppsSize << "\n";					
								break;
							}
						}
						if (ppsSize <0)
						{
							ppsSize = frameSize - spsSize - 2*sizeof(marker);
							std::cout << "PPS size:" << ppsSize  << "\n";					
						}
					}
				}
			}
			if ( (spsSize > 0) && (ppsSize > 0) )
			{
				char sps[spsSize];
				memcpy(&sps, frame+sizeof(marker), spsSize);
				char pps[ppsSize];
				memcpy(&pps, &frame[spsSize+2*sizeof(marker)], ppsSize);									
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
				
				free(sps_base64);
				free(pps_base64);
				
				std::cout << "AuxLine:"  << m_auxLine << " \n";		
			}						
			ret = true;
			delete [] frame;
		}
	}

	return ret;
}
		
void V4L2DeviceSource::processFrame(char * frame, int &frameSize, const timeval &ref) 
{
	int offset = 0;
	timeval tv;
	gettimeofday(&tv, NULL);												
	timeval diff;
	timersub(&tv,&ref,&diff);
	
	if (m_verbose >=2) 		
	{
		printf ("queueFrame\ttimestamp:%d.%06d\tsize:%d diff:%d ms queue:%d data:%02X%02X%02X%02X%02X...\n", ref.tv_sec, ref.tv_usec, frameSize, (int)(diff.tv_sec*1000+diff.tv_usec/1000), m_captureQueue.size(), frame[0], frame[1], frame[2], frame[3], frame[4]);
	}
	if (m_outfile) fwrite(frame, frameSize,1, m_outfile);
}	
		
void V4L2DeviceSource::queueFrame(char * frame, int frameSize, const timeval &tv) 
{
	while (m_captureQueue.size() >= m_queueSize)
	{
		if (m_verbose >=2) 
		{
			envir() << "Queue full size drop frame size:"  << (int)m_captureQueue.size() << " \n";		
		}
		delete m_captureQueue.front();
		m_captureQueue.pop_front();
	}
	m_captureQueue.push_back(new Frame(frame, frameSize, tv));	
	
	// post an event to ask to deliver the frame
	envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
}	


