/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** V4l2RTSPServer.cpp
** 
** V4L2 RTSP server
**
** -------------------------------------------------------------------------*/

#include <dirent.h>

#include <sstream>

#include "logger.h"
#include "V4l2Capture.h"
#include "V4l2Output.h"
#include "DeviceSourceFactory.h"
#include "V4l2RTSPServer.h"

#ifdef HAVE_ALSA
#include "ALSACapture.h"
#endif

StreamReplicator* V4l2RTSPServer::CreateVideoReplicator( 
					const V4L2DeviceParameters& inParam,
					int queueSize, int useThread, int repeatConfig,
					const std::string& outputFile, V4l2IoType ioTypeOut, V4l2Output*& out) {

	StreamReplicator* videoReplicator = NULL;
    std::string videoDev(inParam.m_devName);
	if (!videoDev.empty())
	{
		// Init video capture
		LOG(NOTICE) << "Create V4L2 Source..." << videoDev;
		
		V4l2Capture* videoCapture = V4l2Capture::create(inParam);
		if (videoCapture)
		{
			int outfd = -1;
			
			if (!outputFile.empty())
			{
				V4L2DeviceParameters outparam(outputFile.c_str(), videoCapture->getFormat(), videoCapture->getWidth(), videoCapture->getHeight(), 0, ioTypeOut, inParam.m_verbose);
				out = V4l2Output::create(outparam);
				if (out != NULL)
				{
					outfd = out->getFd();
					LOG(INFO) << "Output fd:" << outfd << " " << outputFile;
				} else {
					LOG(WARN) << "Cannot open output:" << outputFile;
				}
			}
			
			std::string rtpVideoFormat(BaseServerMediaSubsession::getVideoRtpFormat(videoCapture->getFormat()));
			if (rtpVideoFormat.empty()) {
				LOG(FATAL) << "No Streaming format supported for device " << videoDev;
				delete videoCapture;
			} else {
				LOG(NOTICE) << "Create Source ..." << videoDev;
				videoReplicator = DeviceSourceFactory::createStreamReplicator(this->env(), videoCapture->getFormat(), new VideoCaptureAccess(videoCapture), queueSize, useThread, outfd, repeatConfig);
				if (videoReplicator == NULL) 
				{
					LOG(FATAL) << "Unable to create source for device " << videoDev;
					delete videoCapture;
				}
			}
		}
	}
	return videoReplicator;
}

std::string getVideoDeviceName(const std::string & devicePath)
{
	std::string deviceName(devicePath);
	size_t pos = deviceName.find_last_of('/');
	if (pos != std::string::npos) {
		deviceName.erase(0,pos+1);
	}
	return deviceName;
}

#ifdef HAVE_ALSA
/* ---------------------------------------------------------------------------
**  get a "deviceid" from uevent sys file
** -------------------------------------------------------------------------*/
std::string getDeviceId(const std::string& evt) {
    std::string deviceid;
    std::istringstream f(evt);
    std::string key;
    while (getline(f, key, '=')) {
            std::string value;
	    if (getline(f, value)) {
		    if ( (key =="PRODUCT") || (key == "PCI_SUBSYS_ID") ) {
			    deviceid = value;
			    break;
		    }
	    }
    }
    return deviceid;
}

std::string  getV4l2Alsa(const std::string& v4l2device) {
	std::string audioDevice(v4l2device);
	
	std::map<std::string,std::string> videodevices;
	std::string video4linuxPath("/sys/class/video4linux");
	DIR *dp = opendir(video4linuxPath.c_str());
	if (dp != NULL) {
		struct dirent *entry = NULL;
		while((entry = readdir(dp))) {
			std::string devicename;
			std::string deviceid;
			if (strstr(entry->d_name,"video") == entry->d_name) {
				std::string ueventPath(video4linuxPath);
				ueventPath.append("/").append(entry->d_name).append("/device/uevent");
				std::ifstream ifsd(ueventPath.c_str());
				deviceid = std::string(std::istreambuf_iterator<char>{ifsd}, {});
				deviceid.erase(deviceid.find_last_not_of("\n")+1);
			}

			if (!deviceid.empty()) {
				videodevices[entry->d_name] = getDeviceId(deviceid);
			}
		}
		closedir(dp);
	}

	std::map<std::string,std::string> audiodevices;
	int rcard = -1;
	while ( (snd_card_next(&rcard) == 0) && (rcard>=0) ) {
		void **hints = NULL;
		if (snd_device_name_hint(rcard, "pcm", &hints) >= 0) {
			void **str = hints;
			while (*str) {				
				std::ostringstream os;
				os << "/sys/class/sound/card" << rcard << "/device/uevent";

				std::ifstream ifs(os.str().c_str());
				std::string deviceid = std::string(std::istreambuf_iterator<char>{ifs}, {});
				deviceid.erase(deviceid.find_last_not_of("\n")+1);
				deviceid = getDeviceId(deviceid);

				if (!deviceid.empty()) {
					if (audiodevices.find(deviceid) == audiodevices.end()) {
						std::string audioname = snd_device_name_get_hint(*str, "NAME");
						audiodevices[deviceid] = audioname;
					}
				}

				str++;
			}

			snd_device_name_free_hint(hints);
		}
	}

	auto deviceId  = videodevices.find(getVideoDeviceName(v4l2device));
	if (deviceId != videodevices.end()) {
		auto audioDeviceIt = audiodevices.find(deviceId->second);
		
		if (audioDeviceIt != audiodevices.end()) {
			audioDevice = audioDeviceIt->second;
			std::cout <<  v4l2device << "=>" << audioDevice << std::endl;			
		}
	}
	
	
	return audioDevice;
}

StreamReplicator* V4l2RTSPServer::CreateAudioReplicator(
			const std::string& audioDev, const std::list<snd_pcm_format_t>& audioFmtList, int audioFreq, int audioNbChannels, int verbose,
			int queueSize, int useThread) {
	StreamReplicator* audioReplicator = NULL;
	if (!audioDev.empty())
	{
		// find the ALSA device associated with the V4L2 device
		std::string audioDevice = getV4l2Alsa(audioDev);
	
		// Init audio capture
		LOG(NOTICE) << "Create ALSA Source..." << audioDevice;
		
		ALSACaptureParameters param(audioDevice.c_str(), audioFmtList, audioFreq, audioNbChannels, verbose);
		ALSACapture* audioCapture = ALSACapture::createNew(param);
		if (audioCapture) 
		{
			audioReplicator = DeviceSourceFactory::createStreamReplicator(this->env(), 0, audioCapture, queueSize, useThread);
			if (audioReplicator == NULL) 
			{
				LOG(FATAL) << "Unable to create source for device " << audioDevice;
				delete audioCapture;
			}
		}
	}	
	return audioReplicator;
}
#endif
