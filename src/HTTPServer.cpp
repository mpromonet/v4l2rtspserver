/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** HTTPServer.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** HTTP server that serves HLS & MPEG-DASH playlist and segments
**                                                                                    
** -------------------------------------------------------------------------*/


#include <sstream>
#include <fstream>
#include <algorithm>

#include "RTSPServer.hh"
#include "RTSPCommon.hh"
#include <time.h>
#include "ByteStreamMemoryBufferSource.hh"
#include "TCPStreamSink.hh"

#include "HTTPServer.h"

u_int32_t HTTPServer::HTTPClientConnection::fClientSessionId = 0;

void HTTPServer::HTTPClientConnection::sendHeader(const char* contentType, unsigned int contentLength)
{
	// Construct our response:
	snprintf((char*)fResponseBuffer, sizeof fResponseBuffer,
	   "HTTP/1.1 200 OK\r\n"
	   "%s"
	   "Server: LIVE555 Streaming Media v%s\r\n"
           "Access-Control-Allow-Origin: *\r\n" 
	   "Content-Type: %s\r\n"
	   "Content-Length: %d\r\n"
	   "\r\n",
	   dateHeader(),
	   LIVEMEDIA_LIBRARY_VERSION_STRING,
	   contentType,
	   contentLength);

	  // Send the response header 
	  send(fClientOutputSocket, (char const*)fResponseBuffer, strlen((char*)fResponseBuffer), 0);
	  fResponseBuffer[0] = '\0'; // We've already sent the response.  This tells the calling code not to send it again.
}
		
void HTTPServer::HTTPClientConnection::streamSource(const std::string & content)
{
	u_int8_t* buffer = new u_int8_t[content.size()];
	memcpy(buffer, content.c_str(), content.size());
	this->streamSource(ByteStreamMemoryBufferSource::createNew(envir(), buffer, content.size()));
}

void HTTPServer::HTTPClientConnection::streamSource(FramedSource* source)
{
      if (fTCPSink != NULL) 
      {
		fTCPSink->stopPlaying();			       
		Medium::close(fTCPSink);
		fTCPSink = NULL;
      }
      if (fSource != NULL) 
      {
		Medium::close(fSource);
		fSource = NULL;
      }
      if (source != NULL) 
      {
		fTCPSink = TCPStreamSink::createNew(envir(), fClientOutputSocket);
		fTCPSink->startPlaying(*source, afterStreaming, this);
		fSource = source; // we need to keep tracking of source, because sink do not release it
      }
}
		
ServerMediaSubsession* HTTPServer::HTTPClientConnection::getSubsesion(const char* urlSuffix)
{
	ServerMediaSubsession* subsession = NULL;
	ServerMediaSession* session = fOurServer.lookupServerMediaSession(urlSuffix);
	if (session != NULL) 
	{
		ServerMediaSubsessionIterator iter(*session);
		subsession = iter.next();
	}
	return subsession;
}
		
bool HTTPServer::HTTPClientConnection::sendM3u8PlayList(char const* urlSuffix)
{
	ServerMediaSubsession* subsession = this->getSubsesion(urlSuffix);
	if (subsession == NULL) 
	{
		return false;			  
	}

	float duration = subsession->duration();
	if (duration <= 0.0) 
	{
		return false;			  
	}
	
	unsigned int startTime = subsession->getCurrentNPT(NULL);
	HTTPServer* httpServer = (HTTPServer*)(&fOurServer);
	unsigned sliceDuration = httpServer->m_hlsSegment;		  
	std::ostringstream os;
	os  	<< "#EXTM3U\r\n"
		<< "#EXT-X-ALLOW-CACHE:NO\r\n"
		<< "#EXT-X-MEDIA-SEQUENCE:" << startTime <<  "\r\n"
		<< "#EXT-X-TARGETDURATION:" << sliceDuration << "\r\n";

	for (unsigned int slice=0; slice*sliceDuration<duration; slice++)
	{
		os << "#EXTINF:" << sliceDuration << ",\r\n";
		os << urlSuffix << "?segment=" << (startTime+slice*sliceDuration) << "\r\n";
	}
	
	envir() << "send M3u8 playlist:" << urlSuffix <<"\n";
	const std::string& playList(os.str());

	// send response header
	this->sendHeader("application/vnd.apple.mpegurl", playList.size());
	
	// stream body
	this->streamSource(playList);

	return true;			  
}
		
bool HTTPServer::HTTPClientConnection::sendMpdPlayList(char const* urlSuffix)
{
	ServerMediaSubsession* subsession = this->getSubsesion(urlSuffix);
	if (subsession == NULL) 
	{
		return false;			  
	}

	float duration = subsession->duration();
	if (duration <= 0.0) 
	{
		return false;
	}
	
	unsigned int startTime = subsession->getCurrentNPT(NULL);
	HTTPServer* httpServer = (HTTPServer*)(&fOurServer);
	unsigned sliceDuration = httpServer->m_hlsSegment;		  
	std::ostringstream os;
	
	os  << "<?xml version='1.0' encoding='UTF-8'?>\r\n"
		<< "<MPD type='dynamic' xmlns='urn:mpeg:DASH:schema:MPD:2011' profiles='urn:mpeg:dash:profile:full:2011' minimumUpdatePeriod='PT"<< sliceDuration <<"S' minBufferTime='" << sliceDuration << "'>\r\n"
		<< "<Period start='PT0S'><AdaptationSet segmentAlignment='true'><Representation mimeType='video/mp2t' codecs='' >\r\n";

	os << "<SegmentTemplate duration='" << sliceDuration << "' media='" << urlSuffix << "?segment=$Number$' startNumber='" << startTime << "' />\r\n";
	os << "</Representation></AdaptationSet></Period>\r\n";
	os << "</MPD>\r\n";

	envir() << "send MPEG-DASH playlist:" << urlSuffix <<"\n";
	const std::string& playList(os.str());

	// send response header
	this->sendHeader("application/dash+xml", playList.size());
	
	// stream body
	this->streamSource(playList);

	return true;
}

		
void HTTPServer::HTTPClientConnection::handleHTTPCmd_StreamingGET(char const* urlSuffix, char const* fullRequestStr) 
{
	char const* questionMarkPos = strrchr(urlSuffix, '?');
	if (strcmp(urlSuffix, "getVersion") == 0) 
	{
		std::ostringstream os;
		os << VERSION;
		std::string content(os.str());
		this->sendHeader("text/plain", content.size());
		this->streamSource(content);
	}
	else if (strncmp(urlSuffix, "getStreamList", strlen("getStreamList")) == 0) 
	{
		std::ostringstream os;
		HTTPServer* httpServer = (HTTPServer*)(&fOurServer);
		ServerMediaSessionIterator it(*httpServer);
		ServerMediaSession* serverSession = NULL;
		if (questionMarkPos != NULL) {
			questionMarkPos++;
			os << "var " << questionMarkPos << "=";
		}
		os << "[\n";
		bool first = true;
		while ( (serverSession = it.next()) != NULL) {
			if (serverSession->duration() > 0) {
				if (first) 
				{
					first = false;
					os << " ";					
				}
				else 
				{
					os << ",";					
				}
				os << "\"" << serverSession->streamName() << "\"";
				os << "\n";
			}
		}
		os << "]\n";
		std::string content(os.str());
		this->sendHeader("text/plain", content.size());
		this->streamSource(content);
	}
	else if (questionMarkPos == NULL) 
	{
		std::string streamName(urlSuffix);
		std::string ext;

		std::string url(urlSuffix);
		size_t pos = url.find_last_of(".");
		if (pos != std::string::npos)
		{
			streamName.assign(url.substr(0,pos));
			ext.assign(url.substr(pos+1));
		}
		bool ok;
		if (ext == "mpd")
		{
			// MPEG-DASH Playlist
			ok = this->sendMpdPlayList(streamName.c_str());				
		}
		else
		{
			// HLS Playlist
			ok = this->sendM3u8PlayList(streamName.c_str());
		}

		if (!ok)
		{
			// send local files
			std::string url(fullRequestStr);
			size_t pos = url.find_first_of(" ");
			if (pos != std::string::npos)
			{
				url.erase(0,pos+1);
			}
			pos = url.find_first_of(" ");
			if (pos != std::string::npos)
			{
				url.erase(pos);
			}
			pos = url.find_first_of("/");
			if (pos != std::string::npos)
			{
				url.erase(0,1);
			}
			std::string pattern("../");
			while ((pos = url.find(pattern, pos)) != std::string::npos) {
				url.erase(pos, pattern.length());
			}			
			if (url.empty())
			{
				url = "index.html"; 
				ext = "html";
			}
			if (ext=="js") ext ="javascript";
			std::ifstream file(url.c_str());
			if (file.is_open())
			{
				envir() << "send file:" << url.c_str() <<"\n";
				std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
				std::string mime("text/");
				mime.append(ext);
				this->sendHeader(mime.c_str(), content.size());
				this->streamSource(content);
				ok = true;
			}
		}

		if (!ok)
		{
			handleHTTPCmd_notSupported();
			fIsActive = False;
		}
	}
	else
	{
		unsigned offsetInSeconds;
		if (sscanf(questionMarkPos, "?segment=%u", &offsetInSeconds) != 1)
		{
			handleHTTPCmd_notSupported();
			return;			  
		}
		
		std::string streamName(urlSuffix, questionMarkPos-urlSuffix);
		ServerMediaSubsession* subsession = this->getSubsesion(streamName.c_str());
		if (subsession == NULL) 
		{
			handleHTTPCmd_notSupported();
			fIsActive = False;
			return;			  
		}

		// Call "getStreamParameters()" to create the stream's source.  (Because we're not actually streaming via RTP/RTCP, most
		// of the parameters to the call are dummy.)
		++fClientSessionId;
		Port clientRTPPort(0), clientRTCPPort(0), serverRTPPort(0), serverRTCPPort(0);
		netAddressBits destinationAddress = 0;
		u_int8_t destinationTTL = 0;
		Boolean isMulticast = False;
		subsession->getStreamParameters(fClientSessionId, 0, clientRTPPort,clientRTCPPort, -1,0,0, destinationAddress,destinationTTL, isMulticast, serverRTPPort,serverRTCPPort, fStreamToken);

		// Seek the stream source to the desired place, with the desired duration, and (as a side effect) get the number of bytes:
		double dOffsetInSeconds = (double)offsetInSeconds;
		u_int64_t numBytes = 0;
		subsession->seekStream(fClientSessionId, fStreamToken, dOffsetInSeconds, 0.0, numBytes);

		if (numBytes == 0) 
		{
			// For some reason, we do not know the size of the requested range.  We can't handle this request:
			handleHTTPCmd_notSupported();
			fIsActive = False;
		}
		else
		{
			// send response header
			this->sendHeader("video/mp2t", numBytes);

			// stream body
			this->streamSource(subsession->getStreamSource(fStreamToken));
			
			// pointer to subsession to close it
			fSubsession = subsession;
		}
	} 
}

void HTTPServer::HTTPClientConnection::handleCmd_notFound() {
	std::ostringstream os;
	HTTPServer* httpServer = (HTTPServer*)(&fOurServer);
	ServerMediaSessionIterator it(*httpServer);
	ServerMediaSession* serverSession = NULL;
	while ( (serverSession = it.next()) != NULL) {
		os << serverSession->streamName() << "\n";
	}
	
	setRTSPResponse("404 Stream Not Found", os.str().c_str());
}

void HTTPServer::HTTPClientConnection::afterStreaming(void* clientData) 
{	
	HTTPServer::HTTPClientConnection* clientConnection = (HTTPServer::HTTPClientConnection*)clientData;
	
	// Arrange to delete the 'client connection' object:
	if (clientConnection->fRecursionCount > 0) {
		// We're still in the midst of handling a request
		clientConnection->fIsActive = False; // will cause the object to get deleted at the end of handling the request
				
	} else {
		// We're no longer handling a request; delete the object now:
		delete clientConnection;
	}
}

HTTPServer::HTTPClientConnection::~HTTPClientConnection() 
{
	this->streamSource(NULL);
	
	if (fSubsession) {
		fSubsession->deleteStream(fClientSessionId,  fStreamToken);
	}
}
