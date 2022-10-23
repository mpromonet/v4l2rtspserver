/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4L2DeviceSource.h
** 
**  live555 source 
**
** -------------------------------------------------------------------------*/


#pragma once

#include <string>
#include <list> 
#include <iostream>
#include <iomanip>

#include <pthread.h>

// live555
#include <liveMedia.hh>

#include "DeviceInterface.h"

// -----------------------------------------
//    Video Device Source 
// -----------------------------------------
class V4L2DeviceSource: public FramedSource
{
	public:
		// ---------------------------------
		// Captured frame
		// ---------------------------------
		struct Frame
		{
			Frame(char* buffer, int size, timeval timestamp, char * allocatedBuffer = NULL) : m_buffer(buffer), m_size(size), m_timestamp(timestamp), m_allocatedBuffer(allocatedBuffer) {};
			Frame(const Frame&);
			Frame& operator=(const Frame&);
			~Frame()  { delete [] m_allocatedBuffer; };
			
			char* m_buffer;
			unsigned int m_size;
			timeval m_timestamp;
			char* m_allocatedBuffer;
		};
		
		// ---------------------------------
		// Compute simple stats
		// ---------------------------------
		class Stats
		{
			public:
				Stats(const std::string & msg) : m_fps(0), m_fps_sec(0), m_size(0), m_msg(msg) {};
				
			public:
				int notify(int tv_sec, int framesize);
			
			protected:
				int m_fps;
				int m_fps_sec;
				int m_size;
				const std::string m_msg;
		};
		
		// ---------------------------------
		// Capture Mode
		// ---------------------------------
		enum CaptureMode
		{
			CAPTURE_LIVE555_THREAD = 0,
			CAPTURE_INTERNAL_THREAD,
			NOCAPTURE
		};

	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode) ;
		std::string getAuxLine()                   { return m_auxLine;    }
		DeviceInterface* getDevice()               { return m_device;     }	
		void postFrame(char * frame, int frameSize, const timeval &ref);
		virtual std::list< std::string > getInitFrames() { return std::list< std::string >(); }
	

	protected:
		V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode);
		virtual ~V4L2DeviceSource();

	protected:	
		static void* threadStub(void* clientData) { return ((V4L2DeviceSource*) clientData)->thread();};
		virtual void* thread();
		static void deliverFrameStub(void* clientData) {((V4L2DeviceSource*) clientData)->deliverFrame();};
		void deliverFrame();
		static void incomingPacketHandlerStub(void* clientData, int mask) { ((V4L2DeviceSource*) clientData)->incomingPacketHandler(); };
		void incomingPacketHandler();
		int getNextFrame();
		void processFrame(char * frame, int frameSize, const timeval &ref);
		void queueFrame(char * frame, int frameSize, const timeval &tv, char * allocatedBuffer = NULL);

		// split packet in frames
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);
		
		// overide FramedSource
		virtual void doGetNextFrame();	
					
	protected:
		std::list<Frame*> m_captureQueue;
		Stats m_in;
		Stats m_out;
		EventTriggerId m_eventTriggerId;
		int m_outfd;
		DeviceInterface * m_device;
		unsigned int m_queueSize;
		pthread_t m_thid;
		pthread_mutex_t m_mutex;
		std::string m_auxLine;
};

