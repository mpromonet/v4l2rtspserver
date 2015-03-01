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

// project
#include "V4l2Capture.h"

// ---------------------------------
// H264 parsing
// ---------------------------------
const char H264marker[] = {0,0,0,1};
class H264Filter
{
	public:				
		H264Filter() {};
		virtual ~H264Filter() {};
			
		std::string getAuxLine() { return m_auxLine; };
		
		std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize) 
		{				
			std::list< std::pair<unsigned char*,size_t> > frameList;
			
			size_t size = 0;
			unsigned char* buffer = this->extractFrame(frame, frameSize, size);
			while (buffer != NULL)				
			{
				frameList.push_back(std::make_pair<unsigned char*,size_t>(buffer, size));
				switch (buffer[0]&0x1F)					
				{
					case 7: std::cout << "SPS\n"; m_sps.assign((char*)buffer,size); break;
					case 8: std::cout << "PPS\n"; m_pps.assign((char*)buffer,size); break;
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
					std::cout << m_auxLine.c_str() << "\n";
				}
				
				frameSize -= size;				
				buffer = this->extractFrame(&buffer[size], frameSize, size);
			}
			return frameList;
		}
		
	private:		
		unsigned char* extractFrame(unsigned char* frame, size_t size, size_t& outsize)
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
		
	private:
		std::string m_auxLine;
		std::string m_sps;
		std::string m_pps;
};


// ---------------------------------
// V4L2 FramedSource
// ---------------------------------
class V4L2DeviceSource: public FramedSource, public H264Filter
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
			~Frame()  { delete m_buffer; };
			
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
				int notify(int tv_sec, int framesize, int verbose);
			
			protected:
				int m_fps;
				int m_fps_sec;
				int m_size;
				const std::string m_msg;
		};
		
	public:
		static V4L2DeviceSource* createNew(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, int verbose, bool useThread) ;

	protected:
		V4L2DeviceSource(UsageEnvironment& env, V4L2DeviceParameters params, V4l2Capture * device, int outputFd, unsigned int queueSize, int verbose, bool useThread);
		virtual ~V4L2DeviceSource();

	protected:	
		static void* threadStub(void* clientData) { return ((V4L2DeviceSource*) clientData)->thread();};
		void* thread();
		static void deliverFrameStub(void* clientData) {((V4L2DeviceSource*) clientData)->deliverFrame();};
		void deliverFrame();
		static void incomingPacketHandlerStub(void* clientData, int mask) { ((V4L2DeviceSource*) clientData)->getNextFrame(); };
		int getNextFrame();
		void processFrame(char * frame, int frameSize, const timeval &ref);
		void queueFrame(char * frame, int frameSize, const timeval &tv);

		// overide FramedSource
		virtual void doGetNextFrame();	
		virtual void doStopGettingFrames();
					
	protected:
		V4L2DeviceParameters m_params;
		std::list<Frame*> m_captureQueue;
		Stats m_in;
		Stats m_out;
		EventTriggerId m_eventTriggerId;
		int m_outfd;
		V4l2Capture * m_device;
		unsigned int m_queueSize;
		int m_verbose;
		pthread_t m_thid;
};

#endif
