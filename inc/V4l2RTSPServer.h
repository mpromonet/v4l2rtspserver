/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2RTSPServer.h
** 
** V4L2 RTSP server
**
** -------------------------------------------------------------------------*/

#pragma once

#include <list>

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include "HTTPServer.h"
#include "UnicastServerMediaSubsession.h"
#include "MulticastServerMediaSubsession.h"
#include "TSServerMediaSubsession.h"

class V4l2RTSPServer {
    public:
        V4l2RTSPServer(unsigned short rtspPort, unsigned short rtspOverHTTPPort = 0, int timeout = 10, unsigned int hlsSegment = 0, const std::list<std::string> & userPasswordList = std::list<std::string>(), const char* realm = NULL, const std::string & webroot = "", const std::string & sslkeycert = "", bool enableRTSPS = false)
            : m_stop(0)
            , m_env(BasicUsageEnvironment::createNew(*BasicTaskScheduler::createNew()))
            , m_rtspPort(rtspPort)
        {     
            m_rtspServer = HTTPServer::createNew(*m_env, rtspPort, userPasswordList, realm, timeout, hlsSegment, webroot, sslkeycert, enableRTSPS);
           	if (m_rtspServer != NULL)
            {
                if (rtspOverHTTPPort)
                {
                    m_rtspServer->setUpTunnelingOverHTTP(rtspOverHTTPPort);
                }             
            }            
        }

        virtual ~V4l2RTSPServer() {
            Medium::close(m_rtspServer);
            TaskScheduler* scheduler = &(m_env->taskScheduler());
            m_env->reclaim();
            delete scheduler;	
        }

        bool available() {
            return ((m_env != NULL) && (m_rtspServer != NULL));
        }
        std::string getResultMsg() {
            std::string result("UsageEnvironment not exists");
            if (m_env) {
                result = m_env->getResultMsg();
            }
            return result;
        }

        void eventLoop(char * stop) {
            m_env->taskScheduler().doEventLoop(stop); 
        }

        void eventLoop() {
            m_env->taskScheduler().doEventLoop(&m_stop); 
        }

        void stopLoop() {
            m_stop = 1;
        }

        UsageEnvironment* env() {
            return m_env; 
        }

        // -----------------------------------------
        //    create video capture & replicator
        // -----------------------------------------
        StreamReplicator* CreateVideoReplicator( 
					const V4L2DeviceParameters& inParam,
					int queueSize, V4L2DeviceSource::CaptureMode captureMode, int repeatConfig,
					const std::string& outputFile, V4l2IoType ioTypeOut, V4l2Output*& out);

#ifdef HAVE_ALSA
        StreamReplicator* CreateAudioReplicator(
			const std::string& audioDev, const std::list<snd_pcm_format_t>& audioFmtList, int audioFreq, int audioNbChannels, int verbose,
			int queueSize, V4L2DeviceSource::CaptureMode captureMode);
        
        static std::string       getV4l2Alsa(const std::string& v4l2device);
        static snd_pcm_format_t  decodeAudioFormat(const std::string& fmt);
        static std::string       getAudioFormatName(const snd_pcm_format_t fmt) {
            return snd_pcm_format_name(fmt);
        }
#endif

        // -----------------------------------------
        //    Add unicast Session
        // -----------------------------------------
        ServerMediaSession* AddUnicastSession(const std::string& url, StreamReplicator* videoReplicator, StreamReplicator* audioReplicator) {
			// Create Unicast Session					
			std::list<ServerMediaSubsession*> subSession;
			if (videoReplicator)
			{
				subSession.push_back(UnicastServerMediaSubsession::createNew(*this->env(), videoReplicator));				
			}
			if (audioReplicator)
			{
				subSession.push_back(UnicastServerMediaSubsession::createNew(*this->env(), audioReplicator));				
			}
			return this->addSession(url, subSession);	    
        }

        // -----------------------------------------
        //    Add HLS & MPEG# Session
        // -----------------------------------------
        ServerMediaSession* AddHlsSession(const std::string& url, int hlsSegment, StreamReplicator* videoReplicator, StreamReplicator* audioReplicator) {
            std::list<ServerMediaSubsession*> subSession;
            if (videoReplicator)
            {
                subSession.push_back(TSServerMediaSubsession::createNew(*this->env(), videoReplicator, audioReplicator, hlsSegment));				
            }
            ServerMediaSession* sms = this->addSession(url, subSession);
            
            struct in_addr ip;
#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1611878400				
            ip.s_addr = ourIPAddress(*this->env());
#else
            ip.s_addr = ourIPv4Address(*this->env());
#endif
            LOG(NOTICE) << "HLS       http://" << inet_ntoa(ip) << ":" << m_rtspPort << "/" << url << ".m3u8";
            LOG(NOTICE) << "MPEG-DASH http://" << inet_ntoa(ip) << ":" << m_rtspPort << "/" << url << ".mpd";	
			
			return sms;	    
        }        


        // -----------------------------------------
        //    Add multicats Session
        // -----------------------------------------
        ServerMediaSession* AddMulticastSession(const std::string& url, in_addr destinationAddress, unsigned short & rtpPortNum, unsigned short & rtcpPortNum, StreamReplicator* videoReplicator, StreamReplicator* audioReplicator) {

            LOG(NOTICE) << "RTP  address " << inet_ntoa(destinationAddress) << ":" << rtpPortNum;
            LOG(NOTICE) << "RTCP address " << inet_ntoa(destinationAddress) << ":" << rtcpPortNum;
        	unsigned char ttl = 5;
            std::list<ServerMediaSubsession*> subSession;						
            if (videoReplicator)
            {
                subSession.push_back(MulticastServerMediaSubsession::createNew(*this->env(), destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, videoReplicator));					
                // increment ports for next sessions
                rtpPortNum+=2;
                rtcpPortNum+=2;
            }
            
            if (audioReplicator)
            {
                subSession.push_back(MulticastServerMediaSubsession::createNew(*this->env(), destinationAddress, Port(rtpPortNum), Port(rtcpPortNum), ttl, audioReplicator));				
                
                // increment ports for next sessions
                rtpPortNum+=2;
                rtcpPortNum+=2;
            }
            return this->addSession(url, subSession);
        }	

        std::string decodeMulticastUrl(const std::string & maddr, in_addr & destinationAddress, unsigned short & rtpPortNum, unsigned short & rtcpPortNum)
        {
            std::istringstream is(maddr);
            std::string ip;
            getline(is, ip, ':');						
            if (!ip.empty())
            {
                destinationAddress.s_addr = inet_addr(ip.c_str());
            } else {
                destinationAddress.s_addr = chooseRandomIPv4SSMAddress(*this->env());
            }	
            
            rtpPortNum = 20000;
            std::string port;
            getline(is, port, ':');						
            if (!port.empty())
            {
                rtpPortNum = atoi(port.c_str());
            }	
            rtcpPortNum = rtpPortNum+1;
            getline(is, port, ':');						
            if (!port.empty())
            {
                rtcpPortNum = atoi(port.c_str());
            }
            return inet_ntoa(destinationAddress) + std::string(":") + std::to_string(rtpPortNum) + std::string(":") + std::to_string(rtcpPortNum);
        }

        ServerMediaSession* AddMulticastSession(const std::string& url, const std::string& inmulticasturi, std::string& outmulticasturi, StreamReplicator* videoReplicator, StreamReplicator* audioReplicator) {
			struct in_addr destinationAddress;
			unsigned short rtpPortNum;
			unsigned short rtcpPortNum;
            outmulticasturi = this->decodeMulticastUrl(inmulticasturi, destinationAddress, rtpPortNum, rtcpPortNum);
            return this->AddMulticastSession(url, destinationAddress, rtpPortNum, rtcpPortNum, videoReplicator, audioReplicator);
        }

        // -----------------------------------------
        //    get rtsp url
        // -----------------------------------------
        std::string getRtspUrl(ServerMediaSession* sms) {
            std::string url;
            char* rtspurl = m_rtspServer->rtspURL(sms);
            if (rtspurl != NULL)
            {
                url = rtspurl;
                delete[] rtspurl;			
            }
            return url;
        }
        int numClientSessions() {
            return m_rtspServer->numClientSessions();        
        }

        void RemoveSession(ServerMediaSession* sms) {
            m_rtspServer->deleteServerMediaSession(sms);
        }

        void addUserRecord(const char* username, const char* password) {
            m_rtspServer->addUserRecord(username, password);
        }

        std::list<std::string> getUsers() {
            return m_rtspServer->getUsers();
        }

        void setTLS(const std::string & sslCert, bool enableRTSPS = false, bool encryptSRTP = true) {
            m_rtspServer->setTLS(sslCert, enableRTSPS, encryptSRTP);
        }

        bool isRTSPS() { 
            return m_rtspServer->isRTSPS(); 
        }

		bool isSRTP() { 
            return m_rtspServer->isSRTP(); 
        }

		bool isSRTPEncrypted() { 
            return m_rtspServer->isSRTPEncrypted(); 
        }

    protected:
        ServerMediaSession* addSession(const std::string & sessionName, ServerMediaSubsession* subSession)
        {
            std::list<ServerMediaSubsession*> subSessionList;
            if (subSession) {
                subSessionList.push_back(subSession);
            }
            return this->addSession(sessionName, subSessionList);
        }

        ServerMediaSession* addSession(const std::string & sessionName, const std::list<ServerMediaSubsession*> & subSession)
        {
            ServerMediaSession* sms = NULL;
            if (subSession.empty() == false)
            {
                sms = ServerMediaSession::createNew(*m_env, sessionName.c_str());
                if (sms != NULL)
                {
                    std::list<ServerMediaSubsession*>::const_iterator subIt;
                    for (subIt = subSession.begin(); subIt != subSession.end(); ++subIt)
                    {
                        sms->addSubsession(*subIt);
                    }
                    
                    m_rtspServer->addServerMediaSession(sms);

                    char* url = m_rtspServer->rtspURL(sms);
                    if (url != NULL)
                    {
                        LOG(NOTICE) << "Play this stream using the URL \"" << url << "\"";
                        delete[] url;			
                    }
                }
            }
            return sms;
        }

    protected:
        char              m_stop;
        UsageEnvironment* m_env;	
        HTTPServer*       m_rtspServer;
        int               m_rtspPort;
};
