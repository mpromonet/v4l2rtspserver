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

#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

#include "ServerMediaSubsession.h"
#include "UnicastServerMediaSubsession.h"
#include "MulticastServerMediaSubsession.h"
#include "TSServerMediaSubsession.h"
#include "HTTPServer.h"

class V4l2RTSPServer {
    public:
        V4l2RTSPServer(unsigned short rtspPort, unsigned short rtspOverHTTPPort = 0, int timeout = 10, unsigned int hlsSegment = 0, const std::list<std::string> & userPasswordList = std::list<std::string>(), const char* realm = NULL, const std::string & webroot = "")
            : m_stop(0)
            , m_env(BasicUsageEnvironment::createNew(*BasicTaskScheduler::createNew()))
        {     
            UserAuthenticationDatabase* auth = createUserAuthenticationDatabase(userPasswordList, realm);
            m_rtspServer = HTTPServer::createNew(*m_env, rtspPort, auth, timeout, hlsSegment, webroot);
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

        int addSession(const std::string & sessionName, ServerMediaSubsession* subSession)
        {
            std::list<ServerMediaSubsession*> subSessionList;
            if (subSession) {
                subSessionList.push_back(subSession);
            }
            return this->addSession(sessionName, subSessionList);
        }

        int addSession(const std::string & sessionName, const std::list<ServerMediaSubsession*> & subSession)
        {
            int nbSubsession = 0;
            if (subSession.empty() == false)
            {
                ServerMediaSession* sms = ServerMediaSession::createNew(*m_env, sessionName.c_str());
                if (sms != NULL)
                {
                    std::list<ServerMediaSubsession*>::const_iterator subIt;
                    for (subIt = subSession.begin(); subIt != subSession.end(); ++subIt)
                    {
                        sms->addSubsession(*subIt);
                        nbSubsession++;
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
            return nbSubsession;
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
        //    convert V4L2 pix format to RTP mime
        // -----------------------------------------
        static std::string getVideoRtpFormat(int format)
        {
            std::string rtpFormat;
            switch(format)
            {	
                case V4L2_PIX_FMT_HEVC : rtpFormat = "video/H265"; break;
                case V4L2_PIX_FMT_H264 : rtpFormat = "video/H264"; break;
                case V4L2_PIX_FMT_MJPEG: rtpFormat = "video/JPEG"; break;
                case V4L2_PIX_FMT_JPEG : rtpFormat = "video/JPEG"; break;
                case V4L2_PIX_FMT_VP8  : rtpFormat = "video/VP8" ; break;
                case V4L2_PIX_FMT_VP9  : rtpFormat = "video/VP9" ; break;
                case V4L2_PIX_FMT_YUYV : rtpFormat = "video/RAW" ; break;
                case V4L2_PIX_FMT_UYVY : rtpFormat = "video/RAW" ; break;
            }
            
            return rtpFormat;
        }

#ifdef HAVE_ALSA
        // -----------------------------------------
        //    convert string audio format to pcm
        // -----------------------------------------
        static std::string getAudioRtpFormat(snd_pcm_format_t format, int sampleRate, int channels)
        {
            std::ostringstream os;
            os << "audio/";
            switch (format) {
                case SND_PCM_FORMAT_A_LAW:
                    os << "PCMA";
                    break;
                case SND_PCM_FORMAT_MU_LAW:
                    os << "PCMU";
                    break;
                case SND_PCM_FORMAT_S8:
                    os << "L8";
                    break;
                case SND_PCM_FORMAT_S24_BE:
                case SND_PCM_FORMAT_S24_LE:
                    os << "L24";
                    break;
                case SND_PCM_FORMAT_S32_BE:
                case SND_PCM_FORMAT_S32_LE:
                    os << "L32";
                    break;
                case SND_PCM_FORMAT_MPEG:
                    os << "MPEG";
                    break;
                default:
                    os << "L16";
                    break;
            }
            os << "/" << sampleRate << "/" << channels;
            return os.str();
        }
#endif

    protected:
        UserAuthenticationDatabase* createUserAuthenticationDatabase(const std::list<std::string> & userPasswordList, const char* realm)
        {
            UserAuthenticationDatabase* auth = NULL;
            if (userPasswordList.size() > 0)
            {
                auth = new UserAuthenticationDatabase(realm, (realm != NULL) );
                
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

    protected:
        char              m_stop;
        UsageEnvironment* m_env;	
        RTSPServer*       m_rtspServer;
};