/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** DeviceSource.h
** 
**  live555 source 
**
** -------------------------------------------------------------------------*/


#pragma once


#include "DeviceInterface.h"
#include "V4l2Capture.h"

// -----------------------------------------
//    Video Device Capture Interface 
// -----------------------------------------
class VideoCaptureAccess : public DeviceInterface
{
	public:
		VideoCaptureAccess(V4l2Capture* device) : m_device(device), m_storedFps(30) {}
		virtual ~VideoCaptureAccess()                              { delete m_device; }
		
		// Store FPS for later retrieval (workaround for compatibility)
		void setStoredFps(int fps) { m_storedFps = fps; }
			
		virtual size_t read(char* buffer, size_t bufferSize)       { return m_device->read(buffer, bufferSize); }
		virtual int getFd()                                        { return m_device->getFd(); }
		virtual unsigned long getBufferSize()                      { return m_device->getBufferSize(); }
		virtual int getWidth()                                     { return m_device->getWidth(); }
		virtual int getHeight()                                    { return m_device->getHeight(); }
		virtual int getFps()                                       { 
			// Return stored FPS (set during device creation)
			return m_storedFps;
		}
		virtual int getVideoFormat()                               { return m_device->getFormat(); }
			
	protected:
		V4l2Capture* m_device;
		int m_storedFps; // Store FPS for compatibility
};
