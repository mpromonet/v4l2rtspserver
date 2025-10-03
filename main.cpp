/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** main.cpp
** 
** V4L2 RTSP streamer                                                                 
**                                                                                    
** H264 capture using V4L2                                                            
** RTSP using live555                                                                 
**                                                                                    
** -------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>  // for fsync()

#include <sstream>
#include <vector>
#include <list>

// libv4l2
#ifdef __linux__
#include <linux/videodev2.h>
#endif

// live555
#include "UsageEnvironment.hh"
#include "BasicUsageEnvironment.hh"
#include "liveMedia.hh"

// project
#include "logger.h"

#include "V4l2Device.h"
#include "V4l2Output.h"

#include "V4l2RTSPServer.h"
#include "DeviceSourceFactory.h"
#include "SnapshotManager.h"
#include "H264_V4l2DeviceSource.h"

// -----------------------------------------
//    signal handler
// -----------------------------------------
char quit = 0;

// Global list to track active MP4 output file descriptors for proper finalization
static std::list<int> g_mp4OutputFds;

// Function to register MP4 file descriptor (called from V4l2RTSPServer)
extern "C" void registerMP4FileDescriptor(int fd) {
	if (fd != -1) {
		g_mp4OutputFds.push_back(fd);
		printf("Registered MP4 file descriptor %d for finalization\n", fd);
	}
}

// External function for emergency MP4 finalization
extern "C" void forceFinalizeMp4Files();

void sighandler(int n)
{ 
	printf("SIGINT\n");
	
	// CRITICAL: Force finalize MP4 files before exit to prevent data loss
	// Since destructors may not be called on SIGINT, we need to manually sync/close
	for (int fd : g_mp4OutputFds) {
		if (fd != -1) {
			printf("Force syncing MP4 file descriptor %d before exit\n", fd);
			fsync(fd);  // Force flush data to disk
			// Note: Don't close here as it may be closed by destructors
		}
	}
	
	// EMERGENCY: Force finalize MP4 muxers since destructors won't be called
	forceFinalizeMp4Files();
	
	quit = 1;
}

// -------------------------------------------------------
//    split video,audio device
// -------------------------------------------------------
void decodeDevice(const std::string & device, std::string & videoDev, std::string & audioDev)
{
	std::istringstream is(device);
	getline(is, videoDev, ',');						
	getline(is, audioDev);						
}

std::string getDeviceName(const std::string & devicePath)
{
	std::string deviceName(devicePath);
	size_t pos = deviceName.find_last_of('/');
	if (pos != std::string::npos) {
		deviceName.erase(0,pos+1);
	}
	return deviceName;
}

		
// -----------------------------------------
//    entry point
// -----------------------------------------
int main(int argc, char** argv) 
{
	// default parameters
	const char *dev_name = "/dev/video0,/dev/video0";	
	unsigned int format = ~0;
	std::list<unsigned int> videoformatList;
	int width = 0;
	int height = 0;
	int queueSize = 5;
	int fps = 25;
	unsigned short rtspPort = 8554;
	unsigned short rtspOverHTTPPort = 0;
	bool multicast = false;
	int verbose = 0;
	std::string outputFile;
	V4l2IoType ioTypeIn  = IOTYPE_MMAP;
	V4l2IoType ioTypeOut = IOTYPE_MMAP;
	int openflags = O_RDWR | O_NONBLOCK; 
	std::string url = "unicast";
	std::string murl = "multicast";
	std::string tsurl = "ts";
	V4L2DeviceSource::CaptureMode captureMode = V4L2DeviceSource::CAPTURE_INTERNAL_THREAD;
	std::string maddr;
	bool repeatConfig = true;
	int timeout = 65;
	int defaultHlsSegment = 2;
	unsigned int hlsSegment = 0;
	std::string sslKeyCert;
	bool enableRTSPS = false;
	const char* realm = NULL;
	std::list<std::string> userPasswordList;
	std::string webroot;
	int snapshotWidth = 640;
	int snapshotHeight = 480;
	int snapshotSaveInterval = 5; // Default 5 seconds
	std::string snapshotFilePath;
	bool enableDump = false;
	std::string dumpDir;
#ifdef HAVE_ALSA	
	int audioFreq = 44100;
	int audioNbChannels = 2;
	std::list<snd_pcm_format_t> audioFmtList;
	snd_pcm_format_t audioFmt = SND_PCM_FORMAT_UNKNOWN;
#endif	
	const char* defaultPort = getenv("PORT");
	if (defaultPort != NULL) {
		rtspPort = atoi(defaultPort);
	}

	// decode parameters
	int c = 0;     
	while ((c = getopt (argc, argv, "v::Q:O:b:j:J:d::" "I:P:p:m::u:M::ct:S::x:X" "R:U:" "rwBsf::F:W:H:G:" "A:C:a:" "Vh")) != -1)
	{
		switch (c)
		{
			case 'v':	verbose    = 1; if (optarg && *optarg=='v') verbose++;  break;
			case 'Q':	queueSize  = atoi(optarg); break;
			case 'O':	outputFile = optarg; break;
			case 'b':	webroot = optarg; break;
			case 'j':	
					snapshotFilePath = optarg;
				break;
			case 'J':   
			{
				// Parse format: widthxheight or widthxheightxinterval
				int tmpWidth = 640, tmpHeight = 480, tmpInterval = 5;
				int parsed = sscanf(optarg, "%dx%dx%d", &tmpWidth, &tmpHeight, &tmpInterval);
				if (parsed >= 2) {
					snapshotWidth = tmpWidth;
					snapshotHeight = tmpHeight;
					if (parsed >= 3) {
						// Validate interval range: 1-60 seconds
						if (tmpInterval < 1) {
							printf("Warning: Save interval too low (%d), using minimum: 1 second\n", tmpInterval);
							tmpInterval = 1;
						} else if (tmpInterval > 60) {
							printf("Warning: Save interval too high (%d), using maximum: 60 seconds\n", tmpInterval);
							tmpInterval = 60;
						}
						snapshotSaveInterval = tmpInterval;
					}
				} else if (sscanf(optarg, "%dx%d", &tmpWidth, &tmpHeight) == 2) {
					// Fallback for old format
					snapshotWidth = tmpWidth;
					snapshotHeight = tmpHeight;
				}
			}
			break;
			
			// RTSP/RTP
			case 'I':       ReceivingInterfaceAddr  = inet_addr(optarg); break;
			case 'P':	rtspPort                = atoi(optarg); break;
			case 'p':	rtspOverHTTPPort        = atoi(optarg); break;
			case 'u':	url                     = optarg; break;
			case 'm':	multicast = true; murl  = optarg ? optarg : murl; break;
			case 'M':	multicast = true; maddr = optarg ? optarg : maddr; break;
			case 'c':	repeatConfig            = false; break;
			case 't':	timeout                 = atoi(optarg); break;
			case 'S':	hlsSegment              = optarg ? atoi(optarg) : defaultHlsSegment; break;
#ifndef NO_OPENSSL
			case 'x':	sslKeyCert              = optarg; break;
			case 'X':	enableRTSPS             = true; break;			
#endif

			// users
			case 'R':       realm                   = optarg; break;
			case 'U':       userPasswordList.push_back(optarg); break;
			
			// V4L2
			case 'r':	ioTypeIn  = IOTYPE_READWRITE; break;
			case 'w':	ioTypeOut = IOTYPE_READWRITE; break;	
			case 'B':	openflags = O_RDWR; break;	
			case 's':	captureMode = V4L2DeviceSource::CAPTURE_LIVE555_THREAD; break;
			case 'f':	format    = V4l2Device::fourcc(optarg); if (format) {videoformatList.push_back(format);};  break;
			case 'F':	fps       = atoi(optarg); break;
			case 'W':	width     = atoi(optarg); break;
			case 'H':	height    = atoi(optarg); break;
			case 'G':   sscanf(optarg,"%dx%dx%d", &width, &height, &fps); break;
			
			// ALSA
#ifdef HAVE_ALSA	
			case 'A':	audioFreq = atoi(optarg); break;
			case 'C':	audioNbChannels = atoi(optarg); break;
			case 'a':	audioFmt = V4l2RTSPServer::decodeAudioFormat(optarg); if (audioFmt != SND_PCM_FORMAT_UNKNOWN) {audioFmtList.push_back(audioFmt);} ; break;
#endif			
			
			// version
			case 'V':	
				std::cout << VERSION << std::endl;
				exit(0);			
			break;
			
			// help
			case 'h':
			case 'd':
				enableDump = true;
				if (optarg) {
					dumpDir = optarg;
				}
				break;
		}
	}
	std::list<std::string> devList;
	while (optind<argc)
	{
		std::string arg = argv[optind];
		// Skip arguments that look like options but weren't parsed (e.g., -vv after device path)
		if (arg[0] == '-') {
			printf("Warning: Skipping unparsed option '%s' - options must come before device path\n", arg.c_str());
		} else {
			printf("Adding device to list: %s\n", arg.c_str());
			devList.push_back(arg);
		}
		optind++;
	}
	if (devList.empty())
	{
		printf("No devices specified, using default: %s\n", dev_name);
		devList.push_back(dev_name);
	}
	
	// Early debug output before logger is initialized
	printf("Parsed arguments:\n");
	printf("  Output file (-O): %s\n", outputFile.empty() ? "(none)" : outputFile.c_str());
	printf("  Snapshot file (-j): %s\n", snapshotFilePath.empty() ? "(none)" : snapshotFilePath.c_str());
	printf("  Device count: %zu\n", devList.size());
	
	// default format tries
	if ((videoformatList.empty()) && (format!=0)) {
#ifdef __linux__
		videoformatList.push_back(V4L2_PIX_FMT_HEVC);
		videoformatList.push_back(V4L2_PIX_FMT_H264);
		videoformatList.push_back(V4L2_PIX_FMT_MJPEG);
		videoformatList.push_back(V4L2_PIX_FMT_JPEG);
		videoformatList.push_back(V4L2_PIX_FMT_NV12);
#endif
	}

#ifdef HAVE_ALSA	
	// default audio format tries
	if (audioFmtList.empty()) {
		audioFmtList.push_back(SND_PCM_FORMAT_S16_LE);
		audioFmtList.push_back(SND_PCM_FORMAT_S16_BE);
	}
#endif	
	
	// init logger
	initLogger(verbose);
	LOG(NOTICE) << "Version: " << VERSION << " live555 version:" << LIVEMEDIA_LIBRARY_VERSION_STRING;
	
	// Log parsed devices for debugging
	LOG(INFO) << "Parsed " << devList.size() << " device(s):";
	for (std::list<std::string>::iterator it = devList.begin(); it != devList.end(); ++it) {
		LOG(INFO) << "  Device: " << *it;
	}
	if (!outputFile.empty()) {
		LOG(INFO) << "Output file (-O): " << outputFile;
	}
	if (!snapshotFilePath.empty()) {
		LOG(INFO) << "Snapshot file (-j): " << snapshotFilePath;
	}
     	
	
	// create RTSP server
	V4l2RTSPServer rtspServer(rtspPort, rtspOverHTTPPort, timeout, hlsSegment, userPasswordList, realm, webroot, sslKeyCert, enableRTSPS);
	if (!rtspServer.available()) 
	{
		LOG(ERROR) << "Failed to create RTSP server: " << rtspServer.getResultMsg();
	}
	else
	{		
		// decode multicast info
		struct in_addr destinationAddress;
		unsigned short rtpPortNum;
		unsigned short rtcpPortNum;
		rtspServer.decodeMulticastUrl(maddr, destinationAddress, rtpPortNum, rtcpPortNum);	

		std::list<V4l2Output*> outList;
		int nbSource = 0;
		std::list<std::string>::iterator devIt;
		for ( devIt=devList.begin() ; devIt!=devList.end() ; ++devIt)
		{
			std::string deviceName(*devIt);
			
			std::string videoDev;
			std::string audioDev;
			decodeDevice(deviceName, videoDev, audioDev);
			
			std::string baseUrl;
			std::string output(outputFile);
			if (devList.size() > 1)
			{
				baseUrl = getDeviceName(videoDev);
				baseUrl.append("_");
				// output is not compatible with multiple device
				output.assign("");
			}

			V4l2Output* out = NULL;
			V4L2DeviceParameters inParam(videoDev.c_str(), videoformatList, width, height, fps, ioTypeIn, openflags);
			StreamReplicator* videoReplicator = rtspServer.CreateVideoReplicator( 
					inParam,
					queueSize, captureMode, repeatConfig,
					output, ioTypeOut, out);
			if (out != NULL) {
				outList.push_back(out);
			}
			
			// Initialize snapshot manager (always enabled)
			if (videoReplicator != NULL) {
				SnapshotManager::getInstance().setEnabled(true);
				
				// AUTO-DETECT: Get actual frame dimensions from device if not specified
				int actualWidth = width;
				int actualHeight = height;
				
				// If dimensions not specified via -W/-H, they will be 0
				// The V4L2 device will have detected the actual dimensions during CreateVideoReplicator
				if (width == 0 || height == 0) {
					// Note: At this point, the device has been initialized by CreateVideoReplicator
					// and SnapshotManager should have received the correct dimensions via setDeviceFormat()
					// We'll use reasonable defaults and let the system auto-detect from the stream
					actualWidth = (width > 0) ? width : 640;
					actualHeight = (height > 0) ? height : 480;
					LOG(NOTICE) << "Using default dimensions for user interface: " << actualWidth << "x" << actualHeight;
					LOG(NOTICE) << "Note: Actual device dimensions will be auto-detected from video stream";
				}
				
				SnapshotManager::getInstance().setFrameDimensions(actualWidth, actualHeight);
				SnapshotManager::getInstance().setSnapshotResolution(snapshotWidth, snapshotHeight);
				SnapshotManager::getInstance().setSaveInterval(snapshotSaveInterval);
				
				// Configure file saving if path specified
				if (!snapshotFilePath.empty()) {
					SnapshotManager::getInstance().setFilePath(snapshotFilePath);
					LOG(NOTICE) << "Snapshot auto-save enabled to: " << snapshotFilePath << " (interval: " << snapshotSaveInterval << "s)";
				}
				
				if (!SnapshotManager::getInstance().initialize(actualWidth, actualHeight)) {
					LOG(WARN) << "Failed to fully initialize SnapshotManager - falling back to basic mode";
				}
				LOG(NOTICE) << "SnapshotManager mode: " << SnapshotManager::getInstance().getModeDescription();
				
				// Get IP address and port to display full snapshot URL
				struct in_addr ip;
#if LIVEMEDIA_LIBRARY_VERSION_INT	<	1611878400				
				ip.s_addr = ourIPAddress(*rtspServer.env());
#else
				ip.s_addr = ourIPv4Address(*rtspServer.env());
#endif
				
				// Display snapshot URL with full IP and port
				if (rtspOverHTTPPort > 0) {
					LOG(NOTICE) << "Snapshots available at http://" << inet_ntoa(ip) << ":" << rtspOverHTTPPort << "/snapshot";
				} else {
					LOG(NOTICE) << "Snapshots available at http://" << inet_ntoa(ip) << ":" << rtspPort << "/snapshot";
				}
			}
					
			// Init Audio Capture
			StreamReplicator* audioReplicator = NULL;
#ifdef HAVE_ALSA
			audioReplicator = rtspServer.CreateAudioReplicator(
					audioDev, audioFmtList, audioFreq, audioNbChannels, verbose,
					queueSize, captureMode);		
#endif
					
										
			// Create Multicast Session
			if (multicast)						
			{		
				ServerMediaSession* sms = rtspServer.AddMulticastSession(baseUrl+murl, destinationAddress, rtpPortNum, rtcpPortNum, videoReplicator, audioReplicator);
				if (sms) {
					nbSource += sms->numSubsessions();
				}
			}
			
			// Create HLS Session					
			if (hlsSegment > 0)
			{
				ServerMediaSession* sms = rtspServer.AddHlsSession(baseUrl+tsurl, hlsSegment, videoReplicator, audioReplicator);
				if (sms) {
					nbSource += sms->numSubsessions();
				}
			}
			
			// Create Unicast Session
			ServerMediaSession* sms  = rtspServer.AddUnicastSession(baseUrl+url, videoReplicator, audioReplicator);		
			if (sms) {
				nbSource += sms->numSubsessions();
			}
		}

		if (nbSource>0)
		{
			// main loop
			signal(SIGINT,sighandler);
			rtspServer.eventLoop(&quit); 
			LOG(NOTICE) << "Exiting....";			
		}

		while (!outList.empty())
		{
			V4l2Output* out = outList.back();
			delete out;
			outList.pop_back();
		}

		// After initializing SnapshotManager
		if (enableDump) {
			SnapshotManager::getInstance().enableFullDump(dumpDir);
		}
	}
	
	return 0;
}



