/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** HTTPServer.h
** 
** V4L2 RTSP streamer                                                                 
**        
** HTTP server that serves HLS & MPEG-DASH playlist and segments
**                                                                                    
** -------------------------------------------------------------------------*/


#include "RTSPServer.hh"
#include "RTSPCommon.hh"
#include <GroupsockHelper.hh> // for "ignoreSigPipeOnSocket()"


#define TCP_STREAM_SINK_MIN_READ_SIZE 1000
#define TCP_STREAM_SINK_BUFFER_SIZE 10000

class TCPSink: public MediaSink {
	public:
		TCPSink(UsageEnvironment& env, int socketNum) : MediaSink(env), fUnwrittenBytesStart(0), fUnwrittenBytesEnd(0), fInputSourceIsOpen(False), fOutputSocketIsWritable(True),fOutputSocketNum(socketNum) {
			ignoreSigPipeOnSocket(socketNum);
		}

	protected:
		virtual ~TCPSink() {
			envir().taskScheduler().disableBackgroundHandling(fOutputSocketNum);
		}

	protected:
		virtual Boolean continuePlaying() {
			fInputSourceIsOpen = fSource != NULL;
			processBuffer();
			return True;
		}

	private:
		void processBuffer(){
			// First, try writing data to our output socket, if we can:
			if (fOutputSocketIsWritable && numUnwrittenBytes() > 0) {
				int numBytesWritten = send(fOutputSocketNum, (const char*)&fBuffer[fUnwrittenBytesStart], numUnwrittenBytes(), 0);
				if (numBytesWritten < (int)numUnwrittenBytes()) {
					// The output socket is no longer writable.  Set a handler to be called when it becomes writable again.
					fOutputSocketIsWritable = False;
					if (envir().getErrno() != EPIPE) { // on this error, the socket might still be writable, but no longer usable
						envir().taskScheduler().setBackgroundHandling(fOutputSocketNum, SOCKET_WRITABLE, socketWritableHandler, this);
					}
				}
				if (numBytesWritten > 0) {
					// We wrote at least some of our data.  Update our buffer pointers:
					fUnwrittenBytesStart += numBytesWritten;
					if (fUnwrittenBytesStart > fUnwrittenBytesEnd) fUnwrittenBytesStart = fUnwrittenBytesEnd; // sanity check
					if (fUnwrittenBytesStart == fUnwrittenBytesEnd && (!fInputSourceIsOpen || !fSource->isCurrentlyAwaitingData())) {
						fUnwrittenBytesStart = fUnwrittenBytesEnd = 0; // reset the buffer to empty
					}
				}
			}

			// Then, read from our input source, if we can (& we're not already reading from it):
			if (fInputSourceIsOpen && freeBufferSpace() >= TCP_STREAM_SINK_MIN_READ_SIZE && !fSource->isCurrentlyAwaitingData()) {
				fSource->getNextFrame(&fBuffer[fUnwrittenBytesEnd], freeBufferSpace(), afterGettingFrame, this, ourOnSourceClosure, this);
			} else if (!fInputSourceIsOpen && numUnwrittenBytes() == 0) {
				// We're now done:
				onSourceClosure();
			}
		}

		static void socketWritableHandler(void* clientData, int mask) { ((TCPSink*)clientData)->socketWritableHandler(); }
		void socketWritableHandler() {
			envir().taskScheduler().disableBackgroundHandling(fOutputSocketNum); // disable this handler until the next time it's needed
			fOutputSocketIsWritable = True;
			processBuffer();
		}

		static void afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
						struct timeval presentationTime, unsigned durationInMicroseconds) {
			((TCPSink*)clientData)->afterGettingFrame(frameSize, numTruncatedBytes);
		}


		void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes) {
			if (numTruncatedBytes > 0) {
				envir() << "TCPStreamSink::afterGettingFrame(): The input frame data was too large for our buffer.  "
				<< numTruncatedBytes
				<< " bytes of trailing data was dropped!  Correct this by increasing the definition of \"TCP_STREAM_SINK_BUFFER_SIZE\" in \"include/TCPStreamSink.hh\".\n";
			}
			fUnwrittenBytesEnd += frameSize;
			processBuffer();
		}

		static void ourOnSourceClosure(void* clientData) { ((TCPSink*)clientData)->ourOnSourceClosure(); }

		void ourOnSourceClosure() {
			// The input source has closed:
			fInputSourceIsOpen = False;
			processBuffer();
		}

		unsigned numUnwrittenBytes() const { return fUnwrittenBytesEnd - fUnwrittenBytesStart; }
		unsigned freeBufferSpace() const { return TCP_STREAM_SINK_BUFFER_SIZE - fUnwrittenBytesEnd; }

	private:
		unsigned char fBuffer[TCP_STREAM_SINK_BUFFER_SIZE];
		unsigned fUnwrittenBytesStart, fUnwrittenBytesEnd;
		Boolean fInputSourceIsOpen, fOutputSocketIsWritable;
		int fOutputSocketNum;
};

// ---------------------------------------------------------
//  Extend RTSP server to add support for HLS and MPEG-DASH
// ---------------------------------------------------------
#if LIVEMEDIA_LIBRARY_VERSION_INT < 1606435200
#define SOCKETCLIENT sockaddr_in 
#else
#define SOCKETCLIENT sockaddr_storage const&
#endif
class HTTPServer : public RTSPServer
{

	class HTTPClientConnection : public RTSPServer::RTSPClientConnection
	{
		public:
			HTTPClientConnection(RTSPServer& ourServer, int clientSocket, struct SOCKETCLIENT clientAddr)
		       : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr), m_TCPSink(NULL), m_StreamToken(NULL), m_Subsession(NULL), m_Source(NULL) {
			}
			virtual ~HTTPClientConnection();

		private:

			void sendHeader(const char* contentType, unsigned int contentLength);		
			void streamSource(FramedSource* source);	
			void streamSource(const std::string & content);
			ServerMediaSubsession* getSubsesion(const char* urlSuffix);
			bool sendFile(char const* urlSuffix);
			bool sendM3u8PlayList(char const* urlSuffix);
			bool sendMpdPlayList(char const* urlSuffix);
			virtual void handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* fullRequestStr);
			virtual void handleCmd_notFound();
			static void afterStreaming(void* clientData);
		
		private:
			static u_int32_t       m_ClientSessionId;
			TCPSink*               m_TCPSink;
			void*                  m_StreamToken;
			ServerMediaSubsession* m_Subsession;
			FramedSource*          m_Source;
	};
	
	public:
		static HTTPServer* createNew(UsageEnvironment& env, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string webroot) 
		{
			HTTPServer* httpServer = NULL;
#if LIVEMEDIA_LIBRARY_VERSION_INT < 1610928000
			int ourSocketIPv4 = setUpOurSocket(env, rtspPort);
#else
			int ourSocketIPv4 = setUpOurSocket(env, rtspPort, AF_INET);
#endif			
#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1611187200
		  	int ourSocketIPv6 = -1;
#else
		  	int ourSocketIPv6 = setUpOurSocket(env, rtspPort, AF_INET6);
#endif		  

			if (ourSocketIPv4 != -1) 
			{
				httpServer = new HTTPServer(env, ourSocketIPv4, ourSocketIPv6, rtspPort, authDatabase, reclamationTestSeconds, hlsSegment, webroot);
			}
			return httpServer;
		}

#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1611187200
		HTTPServer(UsageEnvironment& env, int ourSocketIPv4, int ourSocketIPv6, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string & webroot)
		  : RTSPServer(env, ourSocketIPv4, rtspPort, authDatabase, reclamationTestSeconds), m_hlsSegment(hlsSegment), m_webroot(webroot)
#else
		HTTPServer(UsageEnvironment& env, int ourSocketIPv4, int ourSocketIPv6, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string & webroot)
		  : RTSPServer(env, ourSocketIPv4, ourSocketIPv6, rtspPort, authDatabase, reclamationTestSeconds), m_hlsSegment(hlsSegment), m_webroot(webroot)
#endif			
		{
                       if ( (!m_webroot.empty()) && (*m_webroot.rend() != '/') ) {
                               m_webroot += "/";
                       }
		}

		RTSPServer::RTSPClientConnection* createNewClientConnection(int clientSocket, struct SOCKETCLIENT clientAddr) 
		{
			return new HTTPClientConnection(*this, clientSocket, clientAddr);
		}
		
        private:
		const unsigned int m_hlsSegment;
		std::string  m_webroot;
};

