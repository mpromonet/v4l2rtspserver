/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2DeviceSource.h
** 
** V4L2 live555 source 
**
** -------------------------------------------------------------------------*/


#ifndef V4L2_DEVICE_SOURCE
#define V4L2_DEVICE_SOURCE

#include <string>
#include <list> 
#include <iostream>
#include <iomanip>

// live555
#include <liveMedia.hh>

// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
class DeviceCapture
{
	public:
		virtual size_t read(char* buffer, size_t bufferSize) = 0;	
		virtual int getFd() = 0;	
		virtual unsigned long getBufferSize() = 0;
		virtual ~DeviceCapture() {};
};

class V4L2DeviceSource: public FramedSource
{
	public:
		// ---------------------------------
		// Captured frame
		// ---------------------------------
		struct Frame
		{
			Frame(char* buffer, int size, timeval timestamp) : m_buffer(buffer), m_size(size), m_timestamp(timestamp) {};
			Frame(const Frame&);
			Frame& operator=(const Frame&);
			~Frame()  { delete [] m_buffer; };
			
			char* m_buffer;
			int m_size;
			timeval m_timestamp;
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
		
	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceCapture * device, int outputFd, unsigned int queueSize, bool useThread) ;
		std::string getAuxLine() { return m_auxLine; };	

	protected:
		V4L2DeviceSource(UsageEnvironment& env, DeviceCapture * device, int outputFd, unsigned int queueSize, bool useThread);
		virtual ~V4L2DeviceSource();

	protected:	
		static void* threadStub(void* clientData) { return ((V4L2DeviceSource*) clientData)->thread();};
		void* thread();
		static void deliverFrameStub(void* clientData) {((V4L2DeviceSource*) clientData)->deliverFrame();};
		void deliverFrame();
		static void incomingPacketHandlerStub(void* clientData, int mask) { ((V4L2DeviceSource*) clientData)->incomingPacketHandler(); };
		void incomingPacketHandler();
		int getNextFrame();
		void processFrame(char * frame, int frameSize, const timeval &ref);
		void queueFrame(char * frame, int frameSize, const timeval &tv);

		// split packet in frames
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);
		
		// overide FramedSource
		virtual void doGetNextFrame();	
		virtual void doStopGettingFrames();
					
	protected:
		std::list<Frame*> m_captureQueue;
		Stats m_in;
		Stats m_out;
		EventTriggerId m_eventTriggerId;
		int m_outfd;
		DeviceCapture * m_device;
		unsigned int m_queueSize;
		pthread_t m_thid;
		pthread_mutex_t m_mutex;
		std::string m_auxLine;
};

#endif
