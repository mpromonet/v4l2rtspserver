/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264_V4l2DeviceSource.cpp
** 
** H264 V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <sstream>
#include <vector>
#include <signal.h>
#include <sys/ioctl.h>
#include <cstring>

#ifdef __linux__
#include <linux/videodev2.h>
#endif

// live555
#include <Base64.hh>

// project
#include "logger.h"
#include "H264_V4l2DeviceSource.h"
#include "SnapshotManager.h"
#include "MP4Muxer.h"

// Simple finalization on exit for MP4 files
static bool g_forceFinalize = false;

// Global list of active MP4Muxers for signal handling
static std::vector<MP4Muxer*> g_activeMuxers;

// External function callable from main.cpp sighandler
extern "C" void forceFinalizeMp4Files() {
	printf("[MP4 Emergency Finalize] Finalizing %zu active MP4 files\n", g_activeMuxers.size());
	for (auto muxer : g_activeMuxers) {
		if (muxer && muxer->isInitialized()) {
			printf("[MP4 Emergency Finalize] Finalizing MP4 muxer\n");
			muxer->finalize();
		}
	}
	g_activeMuxers.clear();
}

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------

H264_V4L2DeviceSource::~H264_V4L2DeviceSource() {
	// CRITICAL: Finalize MP4 BEFORE closing file descriptor
	if (m_mp4Muxer && m_mp4Muxer->isInitialized()) {
		LOG(INFO) << "[H264_V4l2DeviceSource] Finalizing MP4 muxer in destructor";
		m_mp4Muxer->finalize();
	}
	
	// CRITICAL: Also close output file descriptor to trigger data flush
	if (m_outfd != -1) {
		LOG(INFO) << "[H264_V4l2DeviceSource] Closing output file descriptor: " << m_outfd;
		::close(m_outfd);
		m_outfd = -1;
	}
	
	delete m_mp4Muxer;
}

// split packet in frames					
std::list< std::pair<unsigned char*,size_t> > H264_V4L2DeviceSource::splitFrames(unsigned char* frame, unsigned frameSize) 
{				
	std::list< std::pair<unsigned char*,size_t> > frameList;
	
	size_t bufSize = frameSize;
	size_t size = 0;
	int frameType = 0;
	unsigned char* buffer = this->extractFrame(frame, bufSize, size, frameType);
	
	// For proper H264 output file writing
	std::vector<unsigned char> outputBuffer;
	bool hasKeyFrame = false;
	m_currentFrameData.clear();
	m_currentFrameIsKeyframe = false;
	bool frameContainsIDR = false; // NEW: Track if ANY NAL unit in this frame is IDR
	
	while (buffer != NULL)				
	{	
		switch (frameType&0x1F)					
		{
			case 7: LOG(INFO) << "SPS size:" << size << " bufSize:" << bufSize; m_sps.assign((char*)buffer,size); break;
			case 8: LOG(INFO) << "PPS size:" << size << " bufSize:" << bufSize; m_pps.assign((char*)buffer,size); break;
			case 5: 
				LOG(INFO) << "IDR size:" << size << " bufSize:" << bufSize; 
				hasKeyFrame = true;
				frameContainsIDR = true; // NEW: Mark this frame as containing IDR
				
				// Process H264 keyframe for snapshot if enabled
				if (SnapshotManager::getInstance().isEnabled()) {
					// Get actual frame dimensions from device
					int frameWidth = (m_device && m_device->getWidth() > 0) ? m_device->getWidth() : 1920;
					int frameHeight = (m_device && m_device->getHeight() > 0) ? m_device->getHeight() : 1080;
					
					// Pass SPS/PPS data along with keyframe for better snapshot creation
					SnapshotManager::getInstance().processH264KeyframeWithSPS(buffer, size, m_sps, m_pps, frameWidth, frameHeight);
				}
				// FIXED: Avoid duplicating SPS/PPS in stream - they are sent via getInitFrames()
				// This prevents FFmpeg decoding issues caused by redundant parameter sets
				if (m_repeatConfig && !m_sps.empty() && !m_pps.empty())
				{
					LOG(DEBUG) << "Repeating SPS/PPS before IDR frame (size: " << m_sps.size() << "/" << m_pps.size() << ")";
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_sps.c_str(), m_sps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_pps.c_str(), m_pps.size()));
					
					// Add SPS/PPS to output buffer with start codes
					if (m_outfd != -1) {
						// Add start code + SPS
						outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
						outputBuffer.insert(outputBuffer.end(), m_sps.begin(), m_sps.end());
						// Add start code + PPS  
						outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
						outputBuffer.insert(outputBuffer.end(), m_pps.begin(), m_pps.end());
					}
				}
			break;
			default: 
				break;
		}
		
		// Add current NAL unit to output buffer with start code
		if (m_outfd != -1) {
			outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
			outputBuffer.insert(outputBuffer.end(), buffer, buffer + size);
			
			// For MP4 muxer, store ALL frame data (not just keyframes/P/B-frames)
			if (m_isMP4) {
				// Store frame data for MP4 muxer - ALL frames for complete stream
				if (frameType == 5) { // IDR frame (keyframe)
					m_currentFrameData.assign(buffer, buffer + size);
					// Don't set m_currentFrameIsKeyframe here - will be set after loop
				} else if (frameType == 1 || frameType == 2) { // P-frame or B-frame
					m_currentFrameData.assign(buffer, buffer + size);
					// Don't set m_currentFrameIsKeyframe here - will be set after loop
				} else if (frameType != 7 && frameType != 8) { 
					// Include ALL other frame types except SPS/PPS (handled separately)
					// This includes: slice types 6,9,10,11,12 etc for complete stream
					m_currentFrameData.assign(buffer, buffer + size);
					// Don't set m_currentFrameIsKeyframe here - will be set after loop
					LOG(DEBUG) << "Adding non-standard frame type " << frameType << " to MP4 stream";
				}
				// SPS/PPS frames (7,8) are handled separately via initialize()
			}
		}
		
		if (!m_sps.empty() && !m_pps.empty())
		{
			u_int32_t profile_level_id = 0;					
			// Fix: properly extract profile_level_id from SPS (skip NAL unit type byte)
			if (m_sps.size() >= 4) {
				profile_level_id = (((unsigned char)m_sps[1])<<16)|(((unsigned char)m_sps[2])<<8)|((unsigned char)m_sps[3]);
			}
		
			char* sps_base64 = base64Encode(m_sps.c_str(), m_sps.size());
			char* pps_base64 = base64Encode(m_pps.c_str(), m_pps.size());		

			std::ostringstream os; 
			os << "profile-level-id=" << std::hex << std::setw(6) << std::setfill('0') << profile_level_id;
			os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
			m_auxLine.assign(os.str());
			
			delete [] sps_base64;
			delete [] pps_base64;
		}
		frameList.push_back(std::pair<unsigned char*,size_t>(buffer, size));
		
		buffer = this->extractFrame(&buffer[size], bufSize, size, frameType);
	}
	
	// FIXED: Set keyframe status AFTER processing all NAL units in the frame
	if (m_isMP4) {
		m_currentFrameIsKeyframe = frameContainsIDR; // Frame is keyframe if it contains ANY IDR NAL unit
	}
	
	// Write properly formatted H264 data to output file
	if (m_outfd != -1 && !outputBuffer.empty()) {
		if (m_isMP4) {
			// Initialize MP4 muxer on first keyframe for STREAMING (not snapshots)
			if (hasKeyFrame && !m_sps.empty() && !m_pps.empty() && !m_mp4Muxer) {
				m_mp4Muxer = new MP4Muxer();
				
				// IMPROVED: Get frame dimensions from device with better fallback logic
				int frameWidth = 0;
				int frameHeight = 0;
				int frameFps = 30; // Default FPS
				
				if (m_device) {
					frameWidth = m_device->getWidth();
					frameHeight = m_device->getHeight();
					if (m_device->getFps() > 0) {
						frameFps = m_device->getFps();
					}
				}
				
				// If device dimensions are 0 (no -W/-H specified), query the device directly
				if (frameWidth <= 0 || frameHeight <= 0) {
					LOG(WARN) << "[MP4Muxer] Device reports zero dimensions (" << frameWidth << "x" << frameHeight << ")";
					LOG(WARN) << "[MP4Muxer] This usually means -W and -H parameters were not specified";
					LOG(WARN) << "[MP4Muxer] Attempting to query actual device dimensions...";
					
					// Try to get device file descriptor and query format directly
					if (m_device && m_device->getFd() > 0) {
#ifdef __linux__
						struct v4l2_format fmt;
						memset(&fmt, 0, sizeof(fmt));
						fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
						
						if (ioctl(m_device->getFd(), VIDIOC_G_FMT, &fmt) == 0) {
							frameWidth = fmt.fmt.pix.width;
							frameHeight = fmt.fmt.pix.height;
							LOG(INFO) << "[MP4Muxer] Queried actual device dimensions: " << frameWidth << "x" << frameHeight;
						} else {
							LOG(ERROR) << "[MP4Muxer] Failed to query device format: " << strerror(errno);
						}
#endif
					}
					
					// Final fallback if all else fails
					if (frameWidth <= 0 || frameHeight <= 0) {
						frameWidth = 1920;
						frameHeight = 1080;
						LOG(WARN) << "[MP4Muxer] Using hardcoded fallback dimensions: " << frameWidth << "x" << frameHeight;
						LOG(WARN) << "[MP4Muxer] For accurate dimensions, please specify -W and -H parameters";
					}
				} else {
					LOG(INFO) << "[MP4Muxer] Using device-provided dimensions: " << frameWidth << "x" << frameHeight;
				}
				
				if (!m_mp4Muxer->initialize(m_outfd, m_sps, m_pps, frameWidth, frameHeight, frameFps)) {
					LOG(ERROR) << "Failed to initialize MP4 muxer for streaming with dimensions " << frameWidth << "x" << frameHeight;
					delete m_mp4Muxer;
					m_mp4Muxer = nullptr;
					m_isMP4 = false; // Fall back to raw H264
				} else {
					LOG(INFO) << "MP4 streaming muxer initialized successfully: " << frameWidth << "x" << frameHeight << " @ " << frameFps << "fps";
					// Register MP4Muxer for emergency finalization on SIGINT
					g_activeMuxers.push_back(m_mp4Muxer);
				}
			}
			
			// Add frame to MP4 muxer for CONTINUOUS STREAMING
			if (m_mp4Muxer && m_mp4Muxer->isInitialized()) {
				// Add ALL frames (keyframes and non-keyframes) for full stream
				if (!m_currentFrameData.empty()) {
					m_mp4Muxer->addFrame(m_currentFrameData.data(), m_currentFrameData.size(), m_currentFrameIsKeyframe);
					LOG(DEBUG) << "Added frame to MP4 stream: " << m_currentFrameData.size() 
					          << " bytes" << (m_currentFrameIsKeyframe ? " (keyframe)" : "");
					
					// CRITICAL: Periodic sync to prevent data loss (but NOT finalization)
					static int frameCounter = 0;
					frameCounter++;
					if (frameCounter % 50 == 0) {
						LOG(INFO) << "[MP4Muxer] Periodic sync after " << frameCounter << " frames";
						// Just sync data to disk, don't finalize the file
						if (m_mp4Muxer->getFileDescriptor() != -1) {
							fsync(m_mp4Muxer->getFileDescriptor());
							LOG(DEBUG) << "[MP4Muxer] Synced data to disk";
						}
					}
				}
			} else if (!m_mp4Muxer) {
				// If muxer not ready, write raw H264 as fallback
				int written = write(m_outfd, outputBuffer.data(), outputBuffer.size());
				if (written != (int)outputBuffer.size()) {
					LOG(NOTICE) << "H264 fallback write error: " << written << "/" << outputBuffer.size() << " err:" << strerror(errno);
				}
			}
		} else {
			// Raw H264 format
			int written = write(m_outfd, outputBuffer.data(), outputBuffer.size());
			if (written != (int)outputBuffer.size()) {
				LOG(NOTICE) << "H264 output write error: " << written << "/" << outputBuffer.size() << " err:" << strerror(errno);
			} else if (hasKeyFrame) {
				LOG(DEBUG) << "H264 keyframe written to output: " << written << " bytes";
			}
		}
	}
	
	return frameList;
}

std::list< std::string > H264_V4L2DeviceSource::getInitFrames() {
	std::list< std::string > frameList;
	frameList.push_back(this->getFrameWithMarker(m_sps));
	frameList.push_back(this->getFrameWithMarker(m_pps));
	return frameList;
}

bool H264_V4L2DeviceSource::isKeyFrame(const char* buffer, int size) {
	bool res = false;
	if (size > 4)
	{
		int frameType = buffer[4]&0x1F;
		res = (frameType == 5);
	}
	return res;
}

// Simple method to check if output file looks like MP4
bool isMP4Output(int fd) {
	// Simple heuristic: check if we have valid file descriptor
	// In real implementation, this could be passed as a parameter
	// For now, we'll detect based on successful MP4 operations
	return fd > 0; // Will be improved when we integrate with V4l2RTSPServer
}