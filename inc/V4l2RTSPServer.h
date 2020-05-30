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

// live555
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

class V4l2RTSPServer {
    public:
        V4l2RTSPServer(unsigned short rtspPort, unsigned short rtspOverHTTPPort, int timeout, unsigned int hlsSegment, const std::list<std::string> & userPasswordList, const char* realm, const std::string & webroot) {
            m_env = BasicUsageEnvironment::createNew(*BasicTaskScheduler::createNew());
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

        void eventLoop(char * quit) {
            m_env->taskScheduler().doEventLoop(quit); 
        }

        UsageEnvironment* env() {
            return m_env; 
        }

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
        UsageEnvironment* m_env;	
        RTSPServer*       m_rtspServer;
};