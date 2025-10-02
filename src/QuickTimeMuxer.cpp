/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** QuickTimeMuxer.cpp
** 
** Simplified MP4 muxer using live555-compatible MP4 structure
** (Based on QuickTimeFileSink principles but simplified for integration)
**
** -------------------------------------------------------------------------*/

#include "../inc/QuickTimeMuxer.h"
#include "../libv4l2cpp/inc/logger.h"
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <arpa/inet.h>

QuickTimeMuxer::QuickTimeMuxer() 
    : m_initialized(false), m_fd(-1), m_width(0), m_height(0), m_fps(30),
      m_mdatStartPos(0), m_moovStartPos(0), m_currentPos(0), 
      m_frameCount(0), m_keyFrameCount(0) {
}

QuickTimeMuxer::~QuickTimeMuxer() noexcept {
    try {
        if (m_initialized) {
            finalize();
        }
    } catch (...) {
        // Suppress all exceptions in destructor
    }
}

bool QuickTimeMuxer::initialize(int fd, const std::string& sps, const std::string& pps, int width, int height, int fps) {
    if (fd < 0 || sps.empty() || pps.empty() || width <= 0 || height <= 0 || fps <= 0) {
        LOG(ERROR) << "[QuickTimeMuxer] Invalid initialization parameters";
        return false;
    }
    
    m_fd = fd;
    m_sps = sps;
    m_pps = pps;
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_frameCount = 0;
    m_keyFrameCount = 0;
    m_currentPos = 0;
    m_frames.clear();
    
    // Write MP4 header structure
    if (!writeMP4Header()) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to write MP4 header";
        return false;
    }
    
    m_initialized = true;
    LOG(INFO) << "[QuickTimeMuxer] Initialized for " << width << "x" << height << " H264 recording";
    return true;
}

bool QuickTimeMuxer::addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame) {
    if (!m_initialized || !h264Data || dataSize == 0) {
        return false;
    }
    
    // Write frame data with length prefix (MP4 format)
    uint32_t frameSize = static_cast<uint32_t>(dataSize);
    
    // Big-endian format for MP4 compatibility
    uint8_t lenBytes[4];
    lenBytes[0] = (frameSize >> 24) & 0xFF;
    lenBytes[1] = (frameSize >> 16) & 0xFF;
    lenBytes[2] = (frameSize >> 8) & 0xFF;
    lenBytes[3] = frameSize & 0xFF;
    
    writeToFile(lenBytes, 4);
    writeToFile(h264Data, dataSize);
    
    // Save frame metadata
    FrameInfo frameInfo;
    frameInfo.size = dataSize + 4;
    frameInfo.isKeyFrame = isKeyFrame;
    frameInfo.offset = m_currentPos - frameInfo.size;
    m_frames.push_back(frameInfo);
    
    m_frameCount++;
    if (isKeyFrame) {
        m_keyFrameCount++;
    }
    
    LOG(DEBUG) << "[QuickTimeMuxer] Added frame " << m_frameCount << " (" << dataSize << " bytes" 
               << (isKeyFrame ? ", keyframe" : "") << ") at offset " << frameInfo.offset;
    
    return true;
}

bool QuickTimeMuxer::finalize() {
    if (!m_initialized) {
        return false;
    }
    
    // Write moov box with proper metadata
    if (!writeMoovBox()) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to write moov box";
        return false;
    }
    
    LOG(INFO) << "[QuickTimeMuxer] Finalized MP4 file with " << m_frameCount << " frames";
    return true;
}

bool QuickTimeMuxer::writeMP4Header() {
    // Write ftyp box
    auto ftypBox = createFtypBox();
    writeToFile(ftypBox.data(), ftypBox.size());
    
    // Write placeholder moov box (will be updated in finalize)
    m_moovStartPos = m_currentPos;
    auto moovBox = createMinimalMoovBox();
    writeToFile(moovBox.data(), moovBox.size());
    
    // Write mdat box header
    m_mdatStartPos = m_currentPos;
    uint32_t mdatSize = 0; // Will be updated later
    writeToFile(&mdatSize, 4);
    writeToFile("mdat", 4);
    
    return true;
}

bool QuickTimeMuxer::writeMoovBox() {
    // Create proper moov box with video track
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(m_sps.begin(), m_sps.end()),
        std::vector<uint8_t>(m_pps.begin(), m_pps.end()),
        m_width, m_height, m_fps, m_frameCount
    );
    
    // Seek to moov position and overwrite
    if (lseek(m_fd, m_moovStartPos, SEEK_SET) == -1) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to seek to moov position";
        return false;
    }
    
    writeToFile(moovBox.data(), moovBox.size());
    
    // Update mdat size
    size_t mdatSize = m_currentPos - m_mdatStartPos - 8;
    if (lseek(m_fd, m_mdatStartPos, SEEK_SET) == -1) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to seek to mdat position";
        return false;
    }
    
    uint32_t mdatSizeBE = htonl(static_cast<uint32_t>(mdatSize));
    writeToFile(&mdatSizeBE, 4);
    
    return true;
}

void QuickTimeMuxer::writeToFile(const void* data, size_t size) {
    if (m_fd != -1) {
        ssize_t written = write(m_fd, data, size);
        if (written != static_cast<ssize_t>(size)) {
            LOG(ERROR) << "[QuickTimeMuxer] Write error: " << written << "/" << size;
        }
        m_currentPos += written;
    }
}

std::vector<uint8_t> QuickTimeMuxer::createMP4Snapshot(const unsigned char* h264Data, size_t dataSize,
                                                       const std::string& sps, const std::string& pps,
                                                       int width, int height, int fps) {
    // Create a minimal MP4 file in memory
    std::vector<uint8_t> mp4Data;
    
    // Create ftyp box
    auto ftypBox = createFtypBox();
    mp4Data.insert(mp4Data.end(), ftypBox.begin(), ftypBox.end());
    
    // Create moov box
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(sps.begin(), sps.end()),
        std::vector<uint8_t>(pps.begin(), pps.end()),
        width, height, fps, 1
    );
    mp4Data.insert(mp4Data.end(), moovBox.begin(), moovBox.end());
    
    // Create mdat box with frame data
    std::vector<uint8_t> frameData(h264Data, h264Data + dataSize);
    auto mdatBox = createMdatBox(frameData);
    mp4Data.insert(mp4Data.end(), mdatBox.begin(), mdatBox.end());
    
    return mp4Data;
}

std::vector<uint8_t> QuickTimeMuxer::createFtypBox() {
    std::vector<uint8_t> box;
    
    // Box size (32 bytes total)
    write32(box, 32);
    
    // Box type: 'ftyp'
    write32(box, 0x66747970);
    
    // Major brand: 'mp41'
    write32(box, 0x6D703431);
    
    // Minor version
    write32(box, 0);
    
    // Compatible brands
    write32(box, 0x6D703431); // 'mp41'
    write32(box, 0x69736F6D); // 'isom'
    
    return box;
}

std::vector<uint8_t> QuickTimeMuxer::createMinimalMoovBox() {
    std::vector<uint8_t> box;
    
    // Placeholder moov box - will be replaced in finalize
    write32(box, 8);
    write32(box, 0x6D6F6F76); // 'moov'
    
    return box;
}

std::vector<uint8_t> QuickTimeMuxer::createVideoTrackMoovBox(const std::vector<uint8_t>& sps, 
                                                             const std::vector<uint8_t>& pps, 
                                                             int width, int height, int fps, 
                                                             uint32_t frameCount) {
    std::vector<uint8_t> box;
    
    // This is a simplified implementation
    // In a full implementation, this would create proper moov/mvhd/trak/tkhd/mdia/mdhd/hdlr/minf/vmhd/dinf/stbl structure
    
    // For now, return a minimal moov box
    write32(box, 8);
    write32(box, 0x6D6F6F76); // 'moov'
    
    return box;
}

std::vector<uint8_t> QuickTimeMuxer::createMdatBox(const std::vector<uint8_t>& frameData) {
    std::vector<uint8_t> box;
    
    // Box size (8 + data size)
    write32(box, 8 + frameData.size());
    
    // Box type: 'mdat'
    write32(box, 0x6D646174);
    
    // Frame data
    box.insert(box.end(), frameData.begin(), frameData.end());
    
    return box;
}

void QuickTimeMuxer::write32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void QuickTimeMuxer::write16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void QuickTimeMuxer::write8(std::vector<uint8_t>& vec, uint8_t value) {
    vec.push_back(value);
}

std::string QuickTimeMuxer::getNALTypeName(uint8_t nalType) {
    switch (nalType) {
        case 1: return "P-frame";
        case 5: return "IDR-frame";
        case 7: return "SPS";
        case 8: return "PPS";
        case 9: return "AUD";
        default: return "Unknown";
    }
}

std::string QuickTimeMuxer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}