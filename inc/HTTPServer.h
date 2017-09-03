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

// ---------------------------------------------------------
//  Extend RTSP server to add support for HLS and MPEG-DASH
// ---------------------------------------------------------
class HTTPServer : public RTSPServer
{

	class HTTPClientConnection : public RTSPServer::RTSPClientConnection
	{
		public:
			HTTPClientConnection(RTSPServer& ourServer, int clientSocket, struct sockaddr_in clientAddr)
			  : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr), fClientSessionId(0), fTCPSink(NULL) {
			}
			virtual ~HTTPClientConnection();

		private:

			void sendHeader(const char* contentType, unsigned int contentLength);		
			void streamSource(FramedSource* source);		
			ServerMediaSubsession* getSubsesion(const char* urlSuffix);
			bool sendM3u8PlayList(char const* urlSuffix);
			bool sendMpdPlayList(char const* urlSuffix);
			virtual void handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* fullRequestStr);
			virtual void handleCmd_notFound();
			static void afterStreaming(void* clientData);
		
		private:
			u_int32_t fClientSessionId;
			TCPStreamSink* fTCPSink;
	};
	
	public:
		static HTTPServer* createNew(UsageEnvironment& env, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment) 
		{
			HTTPServer* httpServer = NULL;
			int ourSocket = setUpOurSocket(env, rtspPort);
			if (ourSocket != -1) 
			{
				httpServer = new HTTPServer(env, ourSocket, rtspPort, authDatabase, reclamationTestSeconds, hlsSegment);
			}
			return httpServer;
		}

		HTTPServer(UsageEnvironment& env, int ourSocket, Port rtspPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment)
		  : RTSPServer(env, ourSocket, rtspPort, authDatabase, reclamationTestSeconds), m_hlsSegment(hlsSegment)
		{
		}

		RTSPServer::RTSPClientConnection* createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr) 
		{
			return new HTTPClientConnection(*this, clientSocket, clientAddr);
		}
		
		const unsigned int m_hlsSegment;
};

