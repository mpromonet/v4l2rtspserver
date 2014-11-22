/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** logger.h
** 
** -------------------------------------------------------------------------*/

#ifndef LOGGER_H
#define LOGGER_H

#include "log4cpp/Category.hh"
#include "log4cpp/FileAppender.hh"
#include "log4cpp/PatternLayout.hh"


#define LOG(__level)  log4cpp::Category::getRoot() << log4cpp::Priority::__level << __FILE__ << ":" << __LINE__ << " " 

inline void initLogger(int verbose)
{
	// initialize log4cpp
	log4cpp::Category &log = log4cpp::Category::getRoot();
	log4cpp::Appender *app = new log4cpp::FileAppender("root", ::dup(fileno(stdout)));
	if (app)
	{
		log4cpp::PatternLayout *plt = new log4cpp::PatternLayout();
		if (plt)
		{
			plt->setConversionPattern("%d [%p] - %m%n");
			app->setLayout(plt);
		}
		log.addAppender(app);
	}
	switch (verbose)
	{
		case 2: log.setPriority(log4cpp::Priority::DEBUG); break;
		case 1: log.setPriority(log4cpp::Priority::INFO); break;
		default: log.setPriority(log4cpp::Priority::NOTICE); break;
		
	}
	LOG(INFO) << "level:" << log4cpp::Priority::getPriorityName(log.getPriority()); 
}
	
#endif

