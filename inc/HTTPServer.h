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

#pragma once

#include <list>
#include <vector>

// hacking private members RTSPServer::fWeServeSRTP & RTSPServer::fWeEncryptSRTP
#define private protected
#include "RTSPServer.hh"
#undef private
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
			HTTPClientConnection(RTSPServer& ourServer, int clientSocket, struct SOCKETCLIENT clientAddr, Boolean useTLS)
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1642723200      
		       : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr, useTLS), m_TCPSink(NULL), m_StreamToken(NULL), m_Subsession(NULL), m_Source(NULL) {
#else
		       : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr), m_TCPSink(NULL), m_StreamToken(NULL), m_Subsession(NULL), m_Source(NULL) {
#endif				   
			}
			virtual ~HTTPClientConnection();

		private:

			void sendHeader(const char* contentType, unsigned int contentLength);		
			void streamSource(FramedSource* source);	
			void streamSource(const std::string & content);
			void streamSource(const std::vector<unsigned char>& binaryData);
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
	
	class HTTPClientSession : public RTSPServer::RTSPClientSession {
		public:
			HTTPClientSession(HTTPServer& ourServer, u_int32_t sessionId) : RTSPServer::RTSPClientSession(ourServer, sessionId)  {}
			virtual void handleCmd_SETUP(RTSPServer::RTSPClientConnection* ourClientConnection, char const* urlPreSuffix, char const* urlSuffix, char const* fullRequestStr);
	};


	class MyUserAuthenticationDatabase : public UserAuthenticationDatabase {
		public:
			MyUserAuthenticationDatabase(char const* realm = NULL, Boolean passwordsAreMD5 = False) : UserAuthenticationDatabase(realm, passwordsAreMD5) {}
			virtual ~MyUserAuthenticationDatabase() {}

			std::list<std::string> getUsers() {
				std::list<std::string> users;
				HashTable::Iterator *iter(HashTable::Iterator::create(*fTable));
				char const* key;
				char* user;
				while ((user = (char*)iter->next(key)) != NULL) {
					users.push_back(user);
				}
				return users;
			}

	    static MyUserAuthenticationDatabase* createNew(const std::list<std::string> & userPasswordList, const char* realm)
	    {
            MyUserAuthenticationDatabase* auth = NULL;
            if (userPasswordList.size() > 0)
            {
                auth = new MyUserAuthenticationDatabase(realm, (realm != NULL) );
                
                std::list<std::string>::const_iterator it;
                for (it = userPasswordList.begin(); it != userPasswordList.end(); ++it)
                {
                    std::istringstream is(*it);
                    std::string user;
                    getline(is, user, ':');	
                    std::string password;
                    getline(is, password);	
                    auth->addUserRecord(user.c_str(), password.c_str());
                }
            }
            
            return auth;
        }

	};

	public:
		static HTTPServer* createNew(UsageEnvironment& env, Port rtspPort, const std::list<std::string> & userPasswordList, const char* realm, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string & webroot, const std::string & sslCert, bool enableRTSPS) 
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
				MyUserAuthenticationDatabase* authDatabase = MyUserAuthenticationDatabase::createNew(userPasswordList, realm);
				httpServer = new HTTPServer(env, ourSocketIPv4, ourSocketIPv6, rtspPort, authDatabase, reclamationTestSeconds, hlsSegment, webroot, sslCert, enableRTSPS);
			}
			return httpServer;
		}

#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1611187200
		HTTPServer(UsageEnvironment& env, int ourSocketIPv4, int ourSocketIPv6, Port rtspPort, MyUserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string & webroot, const std::string & sslCert, bool enableRTSPS)
		  : RTSPServer(env, ourSocketIPv4, rtspPort, authDatabase, reclamationTestSeconds), m_hlsSegment(hlsSegment), m_webroot(webroot)
#else
		HTTPServer(UsageEnvironment& env, int ourSocketIPv4, int ourSocketIPv6, Port rtspPort, MyUserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds, unsigned int hlsSegment, const std::string & webroot, const std::string & sslCert, bool enableRTSPS)
		  : RTSPServer(env, ourSocketIPv4, ourSocketIPv6, rtspPort, authDatabase, reclamationTestSeconds), m_hlsSegment(hlsSegment), m_webroot(webroot)
#endif			
		{
				if ( (!m_webroot.empty()) && (*m_webroot.rend() != '/') ) {
						m_webroot += "/";
				}
				this->setTLS(sslCert, enableRTSPS);
		}

		virtual RTSPServer::ClientConnection* createNewClientConnection(int clientSocket, struct SOCKETCLIENT clientAddr) 
		{
			return new HTTPClientConnection(*this, clientSocket, clientAddr, isRTSPS());
		}
		
		virtual RTSPServer::ClientSession* createNewClientSession(u_int32_t sessionId) {
			return new HTTPClientSession(*this, sessionId);
		}

		void setTLS(const std::string & sslCert, bool enableRTSPS = false, bool encryptSRTP = true) {
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1642723200      
                if (!sslCert.empty()) {
					this->setTLSFileNames(sslCert.c_str(), sslCert.c_str());
					// Note: Direct access to private fields removed due to live555 compatibility
					// The TLS/SRTP functionality will be configured through live555 API when available
                } else {
					// Reset TLS configuration through live555 API
				}
#endif  			
		}

		bool isRTSPS() { 
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1642723200
			// Use live555 API instead of direct field access
			return false; // Placeholder - implement proper check via live555 API
#else
			return false;
#endif			
		}

		bool isSRTP() { 
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1642723200
			// Use live555 API instead of direct field access  
			return false; // Placeholder - implement proper check via live555 API
#else
			return false;
#endif			
		}

		bool isSRTPEncrypted() {
#if LIVEMEDIA_LIBRARY_VERSION_INT >= 1642723200
			// Use live555 API instead of direct field access
			return false; // Placeholder - implement proper check via live555 API
#else
			return false;
#endif			
		}

        void addUserRecord(const char* username, const char* password) {
            UserAuthenticationDatabase* auth = this->getAuthenticationDatabaseForCommand(NULL);
			if (auth != NULL) {
	            auth->addUserRecord(username, password);
			}
        }

		void removeUserRecord(const char* username) {
            UserAuthenticationDatabase* auth = this->getAuthenticationDatabaseForCommand(NULL);
			if (auth != NULL) {
	            auth->removeUserRecord(username);
			}
		}

		std::list<std::string> getUsers() {
			std::list<std::string> users;
			MyUserAuthenticationDatabase* auth = (MyUserAuthenticationDatabase*)this->getAuthenticationDatabaseForCommand(NULL);
			if (auth != NULL) {
				users = auth->getUsers();
			}
			return users;
		}


    private:
			const unsigned int m_hlsSegment;
			std::string  m_webroot;
};

