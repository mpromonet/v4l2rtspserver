/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** v4l2DeviceSource.cpp
** 
** V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <fcntl.h>
#include <iomanip>
#include <sstream>

// libv4l2
#include <linux/videodev2.h>
#include <libv4l2.h>

// project
#include "logger.h"
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
		LOG(INFO) << m_msg  << "tv_sec:" <<   tv_sec << " fps:" << m_fps << " bandwidth:"<< (m_size/128) << "kbps\n";		
		m_fps_sec = tv_sec;
		m_fps = 0;
		m_size = 0;
	}
	return m_fps;
}

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
V4L2DeviceSource* V4L2DeviceSource::createNew(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, int verbose, bool useThread) 
{ 	
	V4L2DeviceSource* source = NULL;
	if (device)
	{
		source = new V4L2DeviceSource(env, params, device, outputFd, queueSize, verbose, useThread);
	}
	return source;
}

// Constructor
V4L2DeviceSource::V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, int verbose, bool useThread) 
	: FramedSource(env), 
	m_params(params), 
	m_in("in"), 
	m_out("out") , 
	m_outfd(outputFd),
	m_device(device),
	m_queueSize(queueSize),
	m_verbose(verbose)
{
	m_eventTriggerId = envir().taskScheduler().createEventTrigger(V4L2DeviceSource::deliverFrameStub);
	if (m_device)
	{
		if (useThread)
		{
			pthread_create(&m_thid, NULL, threadStub, this);		
		}
		else
		{
			envir().taskScheduler().turnOnBackgroundReadHandling( m_device->getFd(), V4L2DeviceSource::incomingPacketHandlerStub, this);
		}
	}
}

// Destructor
V4L2DeviceSource::~V4L2DeviceSource()
{	
	envir().taskScheduler().deleteEventTrigger(m_eventTriggerId);
	m_device->captureStop();
	pthread_join(m_thid, NULL);	
}

// thread mainloop
void* V4L2DeviceSource::thread()
{
	int stop=0;
	fd_set fdset;
	FD_ZERO(&fdset);
	timeval tv;
	
	LOG(NOTICE) << "begin thread\n"; 
	while (!stop) 
	{
		FD_SET(m_device->getFd(), &fdset);
		tv.tv_sec=1;
		tv.tv_usec=0;	
		int ret = select(m_device->getFd()+1, &fdset, NULL, NULL, &tv);
		if (ret == 1)
		{
			if (FD_ISSET(m_device->getFd(), &fdset))
			{
				if (this->getNextFrame() <= 0)
				{
					LOG(ERROR) << "error:" << strerror(errno) << "\n"; 						
					stop=1;
				}
			}
		}
		else if (ret == -1)
		{
			LOG(ERROR) << "stop " << strerror(errno) << "\n"; 
			stop=1;
		}
	}
	LOG(NOTICE) << "end thread\n"; 
	return NULL;
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
	LOG(NOTICE) << "V4L2DeviceSource::doStopGettingFrames\n";	
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
			LOG(DEBUG) << "Queue is empty \n";		
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

			LOG(DEBUG) << "deliverFrame\ttimestamp:" << fPresentationTime.tv_sec << "." << fPresentationTime.tv_usec << "\tsize:" << fFrameSize <<"\tdiff" <<  (diff.tv_sec*1000+diff.tv_usec/1000) << "ms\tqueue:" << m_captureQueue.size();		
			
			memcpy(fTo, frame->m_buffer, fFrameSize);
			delete frame;
		}
		
		// send Frame to the consumer
		FramedSource::afterGetting(this);			
	}
}
	
// FrameSource callback on read event
int V4L2DeviceSource::getNextFrame() 
{
	char buffer[m_device->getBufferSize()];	
	timeval ref;
	gettimeofday(&ref, NULL);											
	int frameSize = m_device->read(buffer,  m_device->getBufferSize());
	
	if (frameSize < 0)
	{
		LOG(NOTICE) << "V4L2DeviceSource::getNextFrame errno:" << errno << " "  << strerror(errno) << "\n";		
		handleClosure(this);
	}
	else if (frameSize == 0)
	{
		LOG(NOTICE) << "V4L2DeviceSource::getNextFrame no data errno:" << errno << " "  << strerror(errno) << "\n";		
		handleClosure(this);
	}
	else
	{
		timeval tv;
		gettimeofday(&tv, NULL);												
		timeval diff;
		timersub(&tv,&ref,&diff);
		m_in.notify(tv.tv_sec, frameSize, m_verbose);
		LOG(DEBUG) << "getNextFrame\ttimestamp:" << ref.tv_sec << "." << ref.tv_usec << "\tsize:" << frameSize <<"\tdiff" <<  (diff.tv_sec*1000+diff.tv_usec/1000) << "ms\tqueue:" << m_captureQueue.size();
		processFrame(buffer,frameSize,ref);
	}			
	return frameSize;
}	

		
void V4L2DeviceSource::processFrame(char * frame, int frameSize, const timeval &ref) 
{
	timeval tv;
	gettimeofday(&tv, NULL);												
	timeval diff;
	timersub(&tv,&ref,&diff);
		
	std::list< std::pair<unsigned char*,size_t> > frameList = this->splitFrames((unsigned char*)frame, frameSize);
	while (!frameList.empty())
	{
		std::pair<unsigned char*,size_t> & frame = frameList.front();
		size_t size = frame.second;
		char* buf = new char[size];
		memcpy(buf, frame.first, size);
		queueFrame(buf,size,ref);

		LOG(DEBUG) << "queueFrame\ttimestamp:" << ref.tv_sec << "." << ref.tv_usec << "\tsize:" << frameSize <<"\tdiff" <<  (diff.tv_sec*1000+diff.tv_usec/1000) << "ms\tqueue:" << m_captureQueue.size();		
		if (m_outfd != -1) write(m_outfd, buf, size);

		frameList.pop_front();
	}			
}	

// post a frame to fifo
void V4L2DeviceSource::queueFrame(char * frame, int frameSize, const timeval &tv) 
{
	while (m_captureQueue.size() >= m_queueSize)
	{
		LOG(DEBUG) << "Queue full size drop frame size:"  << (int)m_captureQueue.size() << " \n";		
		delete m_captureQueue.front();
		m_captureQueue.pop_front();
	}
	m_captureQueue.push_back(new Frame(frame, frameSize, tv));	
	
	// post an event to ask to deliver the frame
	envir().taskScheduler().triggerEvent(m_eventTriggerId, this);
}	

// split packet in frames					
std::list< std::pair<unsigned char*,size_t> > V4L2DeviceSource::splitFrames(unsigned char* frame, unsigned frameSize) 
{				
	std::list< std::pair<unsigned char*,size_t> > frameList;
	if (frame != NULL)
	{
		frameList.push_back(std::make_pair<unsigned char*,size_t>(frame, frameSize));
	}
	return frameList;
}


	
