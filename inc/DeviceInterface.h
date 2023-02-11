/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** DeviceInterface.h
** 
** -------------------------------------------------------------------------*/


#pragma once
#include <list>

// ---------------------------------
// Device Interface
// ---------------------------------
class DeviceInterface
{
	public:
		virtual size_t read(char* buffer, size_t bufferSize) = 0;	
		virtual int getFd()                                  = 0;	
		virtual unsigned long  getBufferSize()               = 0;
		virtual int            getWidth()                    { return -1; }	
		virtual int            getHeight()                   { return -1; }
		virtual int            getVideoFormat()              { return -1; }
		virtual std::list<int> getVideoFormatList()          { return std::list<int>(); }
		virtual int            getSampleRate()               { return -1; }
		virtual int            getChannels()                 { return -1; }
		virtual int            getAudioFormat()              { return -1; }				
		virtual std::list<int> getAudioFormatList()          { return std::list<int>(); }
		virtual ~DeviceInterface()                           {};
};


