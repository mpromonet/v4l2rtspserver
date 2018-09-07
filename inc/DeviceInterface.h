/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** DeviceInterface.h
** 
** -------------------------------------------------------------------------*/


#ifndef DEVICE_INTERFACE
#define DEVICE_INTERFACE


// ---------------------------------
// Device Interface
// ---------------------------------
class DeviceInterface
{
	public:
		virtual size_t read(char* buffer, size_t bufferSize) = 0;	
		virtual int getFd() = 0;	
		virtual unsigned long getBufferSize() = 0;
		virtual int getWidth() = 0;	
		virtual int getHeight() = 0;	
		virtual int getCaptureFormat() = 0;
		virtual ~DeviceInterface() {};
};


// -----------------------------------------
//    Device Capture Interface template
// -----------------------------------------
template<typename T>
class DeviceCaptureAccess : public DeviceInterface
{
	public:
		DeviceCaptureAccess(T* device) : m_device(device)      {};
		virtual ~DeviceCaptureAccess()                         { delete m_device; };
			
		virtual size_t read(char* buffer, size_t bufferSize) { return m_device->read(buffer, bufferSize); }
		virtual int getFd()                                  { return m_device->getFd(); }
		virtual unsigned long getBufferSize()                { return m_device->getBufferSize(); }
		virtual int getWidth()                               { return m_device->getWidth(); }
		virtual int getHeight()                              { return m_device->getHeight(); }
		virtual int getCaptureFormat()                       { return m_device->getFormat(); }
			
	protected:
		T* m_device;
};

#endif
