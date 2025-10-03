/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManager.cpp
** 
** Real Image Snapshot Manager implementation
**
** -------------------------------------------------------------------------*/

#include "../inc/SnapshotManager.h"
#include "../libv4l2cpp/inc/logger.h"
#include "../inc/QuickTimeMuxer.h"
#include <string>
#include <vector>
#include <fstream>
#include <ctime>

#ifdef __linux__
#include <linux/videodev2.h>
#endif

// Debug dump functionality removed - H264DebugDumper deleted

SnapshotManager::SnapshotManager() 
    : m_enabled(false), m_mode(SnapshotMode::DISABLED), 
      m_width(0), m_height(0), m_snapshotWidth(640), m_snapshotHeight(480), 
      m_lastSnapshotTime(0), m_snapshotMimeType("video/mp4"), m_saveInterval(5), m_lastSaveTime(0),
      m_lastFrameWidth(0), m_lastFrameHeight(0) {
}

SnapshotManager::~SnapshotManager() noexcept {
    // Destructor should not throw exceptions
    try {
        // Any cleanup code if needed in the future
    } catch (...) {
        // Suppress all exceptions in destructor
    }
}

void SnapshotManager::setFrameDimensions(int width, int height) {
    m_width = width;
    m_height = height;
}

void SnapshotManager::setSnapshotResolution(int width, int height) {
    m_snapshotWidth = width > 0 ? width : 640;
    m_snapshotHeight = height > 0 ? height : 480;
    LOG(INFO) << "Snapshot resolution set to: " << m_snapshotWidth << "x" << m_snapshotHeight;
}

void SnapshotManager::setSaveInterval(int intervalSeconds) {
    // Validate range: 1-60 seconds
    if (intervalSeconds < 1) {
        intervalSeconds = 1;
        LOG(WARN) << "Save interval too low, set to minimum: 1 second";
    } else if (intervalSeconds > 60) {
        intervalSeconds = 60;
        LOG(WARN) << "Save interval too high, set to maximum: 60 seconds";
    }
    
    m_saveInterval = intervalSeconds;
    LOG(INFO) << "Snapshot save interval set to: " << m_saveInterval << " seconds";
}

bool SnapshotManager::initialize(int width, int height) {
    m_width = width;
    m_height = height;
    
    if (!m_enabled) {
        m_mode = SnapshotMode::DISABLED;
        return true;
    }
    
    // Default to H264 MP4 mode (using live555-based QuickTimeMuxer)
    m_mode = SnapshotMode::H264_MP4;
    LOG(NOTICE) << "SnapshotManager initialized - Mode: H264 MP4 (via QuickTimeMuxer/live555)";
    return true;
}

void SnapshotManager::processMJPEGFrame(const unsigned char* jpegData, size_t dataSize) {
    if (!m_enabled || !jpegData || dataSize == 0) {
        return;
    }
    
    // Real JPEG data from MJPEG stream (via live555 JPEGVideoSource)
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_currentSnapshot.assign(jpegData, jpegData + dataSize);
        m_snapshotData.assign(jpegData, jpegData + dataSize);
        m_snapshotMimeType = "image/jpeg";
        m_lastSnapshotTime = std::time(nullptr);
        m_lastSnapshotTimePoint = std::chrono::steady_clock::now();
        m_mode = SnapshotMode::MJPEG_STREAM;
        LOG(DEBUG) << "MJPEG snapshot captured: " << dataSize << " bytes";
    }
    
    autoSaveSnapshot();
}

void SnapshotManager::processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    createH264Snapshot(h264Data, dataSize, width, height);
}

void SnapshotManager::processH264KeyframeWithSPS(const unsigned char* h264Data, size_t dataSize, 
                                                const std::string& sps, const std::string& pps, 
                                                int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    createH264Snapshot(h264Data, dataSize, width, height, sps, pps);
}


void SnapshotManager::createH264Snapshot(const unsigned char* h264Data, size_t h264Size, 
                                       int width, int height,
                                       const std::string& sps, const std::string& pps) {
    if (!m_enabled || !h264Data || h264Size == 0) {
        LOG(WARN) << "[H264] Snapshot creation skipped - enabled:" << m_enabled << " data:" << (h264Data ? "valid" : "null") << " size:" << h264Size;
        return;
    }
    
    LOG(DEBUG) << "[H264] Creating MP4 snapshot: " << width << "x" << height << ", SPS:" << sps.size() << "B, PPS:" << pps.size() << "B";

    // Create MP4 snapshot using QuickTimeMuxer (based on live555 QuickTimeFileSink structure)
    std::vector<uint8_t> mp4Data = QuickTimeMuxer::createMP4Snapshot(h264Data, h264Size, sps, pps, width, height);
    
    if (mp4Data.empty()) {
        LOG(ERROR) << "[H264] Failed to create MP4 snapshot";
        return;
    }

    // Store snapshot
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshotData = mp4Data;
        m_snapshotMimeType = "video/mp4";
        m_lastSnapshotTime = std::time(nullptr);
        m_lastSnapshotTimePoint = std::chrono::steady_clock::now();
        m_mode = SnapshotMode::H264_MP4;
        
        // Cache for future use
        m_lastH264Frame.assign(h264Data, h264Data + h264Size);
        if (!sps.empty()) m_lastSPS = sps;
        if (!pps.empty()) m_lastPPS = pps;
        m_lastFrameWidth = width;
        m_lastFrameHeight = height;
        
        LOG(INFO) << "[H264] MP4 snapshot ready: " << mp4Data.size() << " bytes";
    }
    
    autoSaveSnapshot();
}

bool SnapshotManager::getSnapshot(std::vector<unsigned char>& jpegData) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    // Prefer snapshotData if available (for MP4), otherwise use currentSnapshot
    if (!m_snapshotData.empty()) {
        jpegData = m_snapshotData;
        return true;
    }
    
    if (m_currentSnapshot.empty()) {
        return false;
    }
    
    jpegData = m_currentSnapshot;
    return true;
}

std::string SnapshotManager::getSnapshotMimeType() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    // Simple MIME type detection based on mode
    switch (m_mode) {
        case SnapshotMode::MJPEG_STREAM:
            return "image/jpeg";
        case SnapshotMode::H264_MP4:
            return "video/mp4";
        default:
            return "application/octet-stream";
    }
}

std::string SnapshotManager::getModeDescription() const {
    switch (m_mode) {
        case SnapshotMode::DISABLED:
            return "Disabled";
        case SnapshotMode::MJPEG_STREAM:
            return "MJPEG (via live555 JPEGVideoSource)";
        case SnapshotMode::H264_MP4:
            return "H264 MP4 (via QuickTimeMuxer/live555)";
        default:
            return "Unknown";
    }
}

bool SnapshotManager::hasRecentSnapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (m_lastSnapshotTime == 0) {
        return false;
    }
    
    // Consider snapshot recent if it's less than 30 seconds old
    std::time_t now = std::time(nullptr);
    return (now - m_lastSnapshotTime) < 30;
}

void SnapshotManager::autoSaveSnapshot() {
    if (m_filePath.empty()) {
        return;
    }
    
    // Check save interval
    std::time_t now = std::time(nullptr);
    if (m_lastSaveTime > 0 && (now - m_lastSaveTime) < m_saveInterval) {
        // Too soon to save again
        return;
    }
    
    std::vector<unsigned char> dataToSave;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        // Use snapshotData (MP4) if available, otherwise fallback to currentSnapshot (JPEG)
        if (!m_snapshotData.empty()) {
            dataToSave = m_snapshotData;
        } else {
        dataToSave = m_currentSnapshot;
        }
    }
    
    if (dataToSave.empty()) {
        LOG(DEBUG) << "No snapshot data available for auto-save";
        return;
    }
    
    try {
        std::ofstream file(m_filePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(dataToSave.data()), dataToSave.size());
            file.close();
            if (file.good()) {
                m_lastSaveTime = now; // Update save time only on successful save
                LOG(NOTICE) << "Auto-saved snapshot: " << m_filePath << " (" << dataToSave.size() << " bytes)";
            } else {
                LOG(ERROR) << "Error writing snapshot to file: " << m_filePath;
            }
        } else {
            LOG(ERROR) << "Failed to open file for writing: " << m_filePath;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while auto-saving snapshot: " << e.what();
    } catch (...) {
        LOG(ERROR) << "Unknown exception while auto-saving snapshot";
    }
}

bool SnapshotManager::saveSnapshotToFile() {
    if (m_filePath.empty()) {
        LOG(ERROR) << "No file path specified for snapshot saving";
        return false;
    }
    return saveSnapshotToFile(m_filePath);
}

bool SnapshotManager::saveSnapshotToFile(const std::string& filePath) {
    if (!m_enabled) {
        LOG(WARN) << "Snapshots are disabled";
        return false;
    }
    
    std::vector<unsigned char> snapshotData;
    if (!getSnapshot(snapshotData) || snapshotData.empty()) {
        LOG(WARN) << "No snapshot data available for saving";
        return false;
    }
    
    try {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            LOG(ERROR) << "Failed to open file for writing: " << filePath;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(snapshotData.data()), snapshotData.size());
        file.close();
        
        if (file.good()) {
            LOG(NOTICE) << "Snapshot saved to file: " << filePath << " (" << snapshotData.size() << " bytes)";
            return true;
        } else {
            LOG(ERROR) << "Error writing snapshot to file: " << filePath;
            return false;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while saving snapshot: " << e.what();
        return false;
    }
}

void SnapshotManager::enableFullDump(const std::string& dumpDir) {
    m_fullDumpEnabled = true;
    m_fullDumpDir = dumpDir;
} 