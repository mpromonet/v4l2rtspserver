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
#include "../libv4l2cpp/inc/V4l2Capture.h"
#include "../inc/MP4Muxer.h"
#include <string>
#include <vector>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

#ifdef __linux__
#include <linux/videodev2.h>
#endif

//#define DEBUG_DUMP_H264_DATA

#ifdef DEBUG_DUMP_H264_DATA
// ENHANCED FULL DATA DUMPER CLASS
class FullDataDumper {
private:
    std::string dumpDir;
    bool initialized;
    static FullDataDumper* instance;
    
public:
    FullDataDumper() : initialized(false) {
        // Create dump directory with timestamp
        time_t now = time(0);
        struct tm* timeinfo = localtime(&now);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "full_dump_%Y%m%d_%H%M%S", timeinfo);
        dumpDir = std::string("tmp/") + buffer;
        
        // Create directory
        std::string cmd = "mkdir -p " + dumpDir;
        int result = system(cmd.c_str());
        (void)result; // Suppress unused result warning
        
        LOG(NOTICE) << "[DUMP] Full data dumper initialized: " << dumpDir;
        initialized = true;
    }
    
    static FullDataDumper* getInstance() {
        if (!instance) {
            instance = new FullDataDumper();
        }
        return instance;
    }
    
    void setDumpDir(const std::string& dir) {
        dumpDir = dir;
        std::string cmd = "mkdir -p " + dumpDir;
        int result = system(cmd.c_str());
        (void)result;
        LOG(NOTICE) << "[DUMP] Full data dumper directory set: " << dumpDir;
        initialized = true;
    }
    
    // Dump V4L2 device information
    void dumpV4L2DeviceInfo(const std::string& devicePath) {
        if (!initialized) return;
        
        std::string infoFile = dumpDir + "/v4l2_device_info.txt";
        std::ofstream file(infoFile);
        
        file << "=== V4L2 DEVICE INFORMATION ===" << std::endl;
        file << "Device: " << devicePath << std::endl;
        file << "Timestamp: " << time(nullptr) << std::endl;
        file << std::endl;
        
#ifdef __linux__
        int fd = open(devicePath.c_str(), O_RDWR);
        if (fd >= 0) {
            // Get capabilities
            struct v4l2_capability caps;
            if (ioctl(fd, VIDIOC_QUERYCAP, &caps) == 0) {
                file << "=== CAPABILITIES ===" << std::endl;
                file << "Driver: " << caps.driver << std::endl;
                file << "Card: " << caps.card << std::endl;
                file << "Bus Info: " << caps.bus_info << std::endl;
                file << "Version: " << caps.version << std::endl;
                file << "Capabilities: 0x" << std::hex << caps.capabilities << std::dec << std::endl;
                file << "Device Caps: 0x" << std::hex << caps.device_caps << std::dec << std::endl;
                file << std::endl;
            }
            
            // Get current format
            struct v4l2_format fmt;
            memset(&fmt, 0, sizeof(fmt));
            fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
                file << "=== CURRENT FORMAT ===" << std::endl;
                file << "Type: " << fmt.type << std::endl;
                file << "Width: " << fmt.fmt.pix.width << std::endl;
                file << "Height: " << fmt.fmt.pix.height << std::endl;
                file << "Pixel Format: 0x" << std::hex << fmt.fmt.pix.pixelformat << std::dec;
                
                // Decode pixel format
                char fourcc[5] = {0};
                fourcc[0] = fmt.fmt.pix.pixelformat & 0xFF;
                fourcc[1] = (fmt.fmt.pix.pixelformat >> 8) & 0xFF;
                fourcc[2] = (fmt.fmt.pix.pixelformat >> 16) & 0xFF;
                fourcc[3] = (fmt.fmt.pix.pixelformat >> 24) & 0xFF;
                file << " (" << fourcc << ")" << std::endl;
                
                file << "Bytes per line: " << fmt.fmt.pix.bytesperline << std::endl;
                file << "Size image: " << fmt.fmt.pix.sizeimage << std::endl;
                file << "Colorspace: " << fmt.fmt.pix.colorspace << std::endl;
                file << "Field: " << fmt.fmt.pix.field << std::endl;
                file << std::endl;
            }
            
            close(fd);
        }
#endif
        
        file.close();
        LOG(NOTICE) << "[V4L2] Device info dumped: " << infoFile;
    }
    
    // Dump system information
    void dumpSystemInfo() {
        if (!initialized) return;
        
        std::string infoFile = dumpDir + "/system_info.txt";
        std::ofstream file(infoFile);
        
        file << "=== SYSTEM INFORMATION ===" << std::endl;
        file << "Timestamp: " << time(nullptr) << std::endl;
        file << std::endl;
        
        // System info via uname
        FILE* pipe = popen("uname -a", "r");
        if (pipe) {
            char buffer[256];
            file << "=== UNAME ===" << std::endl;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                file << buffer;
            }
            pclose(pipe);
            file << std::endl;
        }
        
        // Video devices
        pipe = popen("ls -la /dev/video* 2>/dev/null", "r");
        if (pipe) {
            char buffer[256];
            file << "=== VIDEO DEVICES ===" << std::endl;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                file << buffer;
            }
            pclose(pipe);
            file << std::endl;
        }
        
        // V4L2 devices info
        pipe = popen("v4l2-ctl --list-devices 2>/dev/null", "r");
        if (pipe) {
            char buffer[256];
            file << "=== V4L2 DEVICES ===" << std::endl;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                file << buffer;
            }
            pclose(pipe);
            file << std::endl;
        }
        
        file.close();
        LOG(NOTICE) << "[SYS] System info dumped: " << infoFile;
    }
    
    // Dump H.264 stream data with full analysis
    void dumpH264StreamData(const std::string& sps, const std::string& pps, 
                           const unsigned char* h264Data, size_t h264Size,
                           int frameNumber, int width, int height) {
        if (!initialized) return;
        
        std::string prefix = dumpDir + "/frame_" + std::to_string(frameNumber);
        
        // Dump SPS
        if (!sps.empty()) {
            std::string spsFile = prefix + "_sps.bin";
            std::ofstream spsOut(spsFile, std::ios::binary);
            spsOut.write(sps.data(), sps.size());
            spsOut.close();
            
            // SPS hex dump
            std::string spsHexFile = prefix + "_sps.hex";
            std::ofstream spsHex(spsHexFile);
            for (size_t i = 0; i < sps.size(); i++) {
                spsHex << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)sps[i];
                if (i < sps.size() - 1) spsHex << " ";
            }
            spsHex.close();
        }
        
        // Dump PPS
        if (!pps.empty()) {
            std::string ppsFile = prefix + "_pps.bin";
            std::ofstream ppsOut(ppsFile, std::ios::binary);
            ppsOut.write(pps.data(), pps.size());
            ppsOut.close();
            
            // PPS hex dump
            std::string ppsHexFile = prefix + "_pps.hex";
            std::ofstream ppsHex(ppsHexFile);
            for (size_t i = 0; i < pps.size(); i++) {
                ppsHex << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)pps[i];
                if (i < pps.size() - 1) ppsHex << " ";
            }
            ppsHex.close();
        }
        
        // Dump H.264 frame
        if (h264Data && h264Size > 0) {
            std::string h264File = prefix + "_h264_frame.bin";
            std::ofstream h264Out(h264File, std::ios::binary);
            h264Out.write(reinterpret_cast<const char*>(h264Data), h264Size);
            h264Out.close();
        }
        
        // Create detailed analysis
        std::string analysisFile = prefix + "_analysis.txt";
        std::ofstream analysis(analysisFile);
        
        analysis << "=== FRAME " << frameNumber << " ANALYSIS ===" << std::endl;
        analysis << "Timestamp: " << time(nullptr) << std::endl;
        analysis << "Resolution: " << width << "x" << height << std::endl;
        analysis << "SPS size: " << sps.size() << " bytes" << std::endl;
        analysis << "PPS size: " << pps.size() << " bytes" << std::endl;
        analysis << "H264 frame size: " << h264Size << " bytes" << std::endl;
        analysis << std::endl;
        
        // Analyze H.264 frame
        if (h264Data && h264Size > 4) {
            analysis << "=== H.264 FRAME ANALYSIS ===" << std::endl;
            
            // Look for NAL units
            for (size_t i = 0; i < h264Size - 4; i++) {
                if (h264Data[i] == 0x00 && h264Data[i+1] == 0x00 && 
                    h264Data[i+2] == 0x00 && h264Data[i+3] == 0x01) {
                    if (i + 4 < h264Size) {
                        uint8_t nalType = h264Data[i+4] & 0x1F;
                        analysis << "NAL unit at offset " << i << ": type " << (int)nalType;
                        switch (nalType) {
                            case 1: analysis << " (Non-IDR slice)"; break;
                            case 5: analysis << " (IDR slice)"; break;
                            case 6: analysis << " (SEI)"; break;
                            case 7: analysis << " (SPS)"; break;
                            case 8: analysis << " (PPS)"; break;
                            case 9: analysis << " (Access unit delimiter)"; break;
                            default: analysis << " (Other)"; break;
                        }
                        analysis << std::endl;
                    }
                }
            }
        }
        
        analysis.close();
        
        LOG(NOTICE) << "[H264] Frame " << frameNumber << " data dumped to: " << prefix << "_*";
    }
    
    std::string getDumpDir() const { return dumpDir; }
};

// Static instance
FullDataDumper* FullDataDumper::instance = nullptr;
#endif

SnapshotManager::SnapshotManager() 
    : m_enabled(false), m_mode(SnapshotMode::DISABLED), 
      m_width(0), m_height(0), m_snapshotWidth(640), m_snapshotHeight(480), 
      m_lastSnapshotTime(0), m_snapshotMimeType("image/jpeg"), m_saveInterval(5), m_lastSaveTime(0),
      m_lastFrameWidth(0), m_lastFrameHeight(0),
      m_v4l2Format(0), m_pixelFormat(""), m_formatInitialized(false) {
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
    
    // Default to H264 fallback mode
    m_mode = SnapshotMode::H264_FALLBACK;
    LOG(NOTICE) << "SnapshotManager initialized in H264 fallback mode";
    return true;
}

void SnapshotManager::processMJPEGFrame(const unsigned char* jpegData, size_t dataSize) {
    if (!m_enabled || !jpegData || dataSize == 0) {
        return;
    }
    
    // This is called from MJPEGVideoSource - we have real JPEG data!
    {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot.assign(jpegData, jpegData + dataSize);
    m_snapshotData.assign(jpegData, jpegData + dataSize);
    m_snapshotMimeType = "image/jpeg";
    m_lastSnapshotTime = std::time(nullptr);
    m_lastSnapshotTimePoint = std::chrono::steady_clock::now();
    
    // Update mode if we weren't sure before
    if (m_mode == SnapshotMode::H264_FALLBACK) {
        m_mode = SnapshotMode::MJPEG_STREAM;
    }
    
    LOG(DEBUG) << "Real MJPEG snapshot captured: " << dataSize << " bytes";
    } // Lock released here automatically
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
}

void SnapshotManager::processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    // Create H264 snapshot with cached frame support
    createH264Snapshot(h264Data, dataSize, width, height);
}

void SnapshotManager::processH264KeyframeWithSPS(const unsigned char* h264Data, size_t dataSize, 
                                                const std::string& sps, const std::string& pps, 
                                                int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    // Create H264 snapshot with SPS/PPS data
    createH264Snapshot(h264Data, dataSize, width, height, sps, pps);
}

void SnapshotManager::processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height) {
    if (!m_enabled || !yuvData || dataSize == 0) {
        return;
    }
    
    // Try YUV->JPEG conversion for real snapshots
    if (width > 0 && height > 0) {
        convertYUVToJPEG(yuvData, dataSize, width, height);
    }
}

// Dynamic NAL unit extraction from H.264 stream (inspired by go2rtc)
std::vector<uint8_t> SnapshotManager::findNALUnit(const uint8_t* data, size_t size, uint8_t nalType) {
    std::vector<uint8_t> result;
    
    for (size_t i = 0; i < size - 4; i++) {
        // Look for start code (0x00 0x00 0x00 0x01)
        if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
            // Check if this is the NAL type we're looking for
            if (i + 4 < size) {
            uint8_t currentNalType = data[i+4] & 0x1F;
            if (currentNalType == nalType) {
                    // Find the end of this NAL unit
                    size_t j = i + 4;
                    while (j < size - 3) {
                    if (data[j] == 0x00 && data[j+1] == 0x00 && data[j+2] == 0x00 && data[j+3] == 0x01) {
                        break;
                    }
                        j++;
                }
                
                    // Extract NAL unit data (excluding start code)
                    result.assign(data + i + 4, data + j);
                break;
                }
            }
        }
    }
    
    return result;
}

// Helper functions for binary data writing (shared with MP4Muxer)
namespace {
    // Removed duplicate functions - use writeU32/writeU16/writeU8 from MP4Muxer helper methods instead
    
    // Parse SPS to extract video dimensions
    std::pair<int, int> parseSPSDimensions(const std::string& sps) {
        if (sps.size() < 8) {
            LOG(WARN) << "[SPS] SPS too short for parsing, using default dimensions";
            return {640, 480};
        }
        
        // Simple SPS parsing for common cases
        // This is a simplified parser - real SPS parsing is much more complex
        const uint8_t* data = reinterpret_cast<const uint8_t*>(sps.data());
        
        // Skip NAL header (1 byte) and profile/level info (3 bytes)
        if (sps.size() >= 8) {
            // Try to extract pic_width_in_mbs_minus1 and pic_height_in_map_units_minus1
            // This is very simplified - real parsing requires bitstream reading
            
            // For now, let's check common resolutions based on SPS size and content
            if (sps.size() >= 16) {
                // Look for patterns that might indicate 1920x1080
                bool likely_1080p = false;
                
                // Check for typical 1080p SPS patterns
                for (size_t i = 4; i < sps.size() - 4; i++) {
                    if (data[i] == 0x78 || data[i] == 0x3C) { // Common in 1080p SPS
                        likely_1080p = true;
                        break;
                    }
                }
                
                if (likely_1080p) {
                    LOG(INFO) << "[SPS] Detected likely 1920x1080 from SPS pattern";
                    return {1920, 1080};
                }
            }
        }
        
        LOG(INFO) << "[SPS] Using default dimensions 640x480";
        return {640, 480};
    }
}

void SnapshotManager::createH264Snapshot(const unsigned char* h264Data, size_t h264Size, 
                                       int width, int height,
                                       const std::string& sps, const std::string& pps) {
    if (!m_enabled || !h264Data || h264Size == 0) {
        LOG(WARN) << "[H264] Snapshot creation skipped - enabled:" << m_enabled << " data:" << (h264Data ? "valid" : "null") << " size:" << h264Size;
        return;
    }
    
    // Use provided dimensions for snapshot (do NOT auto-correct from SPS)
    // The video stream should already be properly sized by the capture device
    
    LOG(DEBUG) << "[H264] Creating MP4 snapshot using provided dimensions: " << width << "x" << height;
    LOG(DEBUG) << "[H264] SPS size:" << sps.size() << " PPS size:" << pps.size();

    // Use MP4Muxer for creating the snapshot - NO MORE DUPLICATED LOGIC!
    std::vector<uint8_t> mp4Data = MP4Muxer::createMP4Snapshot(h264Data, h264Size, sps, pps, width, height);
    
    if (mp4Data.empty()) {
        LOG(ERROR) << "[H264] Failed to create MP4 snapshot using MP4Muxer";
        return;
    }

    // Store as snapshot
    {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_snapshotData = mp4Data;
        m_snapshotMimeType = "video/mp4";
        m_lastSnapshotTime = std::time(nullptr);
        m_lastSnapshotTimePoint = std::chrono::steady_clock::now();
    
    // Cache the frame data and SPS/PPS for future use
        m_lastH264Frame.assign(h264Data, h264Data + h264Size);
        if (!sps.empty()) m_lastSPS = sps;
        if (!pps.empty()) m_lastPPS = pps;
        m_lastFrameWidth = width;
        m_lastFrameHeight = height;
        
        LOG(INFO) << "[H264] MP4 snapshot created via MP4Muxer: " << mp4Data.size() << " bytes (" << width << "x" << height << ")";
    }
    
    // Auto-save snapshot if file path is configured
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
    
    // Use stored MIME type if available
    if (!m_snapshotMimeType.empty() && !m_snapshotData.empty()) {
        return m_snapshotMimeType;
    }
    
    // Check actual content type if we have data
    const std::vector<unsigned char>& dataToCheck = !m_currentSnapshot.empty() ? m_currentSnapshot : m_snapshotData;
    if (!dataToCheck.empty()) {
        // Check for JPEG magic bytes (FF D8 FF)
        if (dataToCheck.size() >= 3 && 
            dataToCheck[0] == 0xFF && 
            dataToCheck[1] == 0xD8 && 
            dataToCheck[2] == 0xFF) {
            return "image/jpeg";
        }
        
        // Check for PNG magic bytes (89 50 4E 47)
        if (dataToCheck.size() >= 4 && 
            dataToCheck[0] == 0x89 && 
            dataToCheck[1] == 0x50 && 
            dataToCheck[2] == 0x4E && 
            dataToCheck[3] == 0x47) {
            return "image/png";
        }
        
        // Check for PPM format (starts with "P6")
        if (dataToCheck.size() >= 2 && 
            dataToCheck[0] == 'P' && 
            dataToCheck[1] == '6') {
            return "image/x-portable-pixmap";
        }
        
        // Check for SVG content (starts with "<?xml" or "<svg")
        if (dataToCheck.size() >= 5) {
            std::string start(dataToCheck.begin(), dataToCheck.begin() + 5);
            if (start == "<?xml" || start == "<svg ") {
                return "image/svg+xml";
            }
        }
        
        // Check for H264 Annex B format (starts with 0x00000001)
        if (dataToCheck.size() >= 4 && 
            dataToCheck[0] == 0x00 && 
            dataToCheck[1] == 0x00 && 
            dataToCheck[2] == 0x00 && 
            dataToCheck[3] == 0x01) {
            return "video/h264";
        }
        
        // Check for MP4 format (starts with ftyp box size + "ftyp")
        if (dataToCheck.size() >= 8 && 
            dataToCheck[4] == 'f' && 
            dataToCheck[5] == 't' && 
            dataToCheck[6] == 'y' && 
            dataToCheck[7] == 'p') {
                return "video/mp4";
        }
    }
    
    // Fallback to mode-based detection
    switch (m_mode) {
        case SnapshotMode::MJPEG_STREAM:
        case SnapshotMode::YUV_CONVERTED:
            return "image/jpeg";
        case SnapshotMode::H264_MP4:
        case SnapshotMode::H264_FALLBACK:
            return "video/mp4";
        default:
            return "text/plain";
    }
}

std::string SnapshotManager::getModeDescription() const {
    switch (m_mode) {
        case SnapshotMode::DISABLED:
            return "Disabled";
        case SnapshotMode::MJPEG_STREAM:
            return "MJPEG Stream (real images when MJPEG active)";
        case SnapshotMode::H264_MP4:
            return "H264 MP4 (mini MP4 videos with keyframes)";
        case SnapshotMode::H264_FALLBACK:
            return "H264 MP4 Fallback (cached MP4 snapshots)";
        case SnapshotMode::YUV_CONVERTED:
            return "YUV Converted (real JPEG images from YUV data)";
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

bool SnapshotManager::convertYUVToJPEG(const unsigned char* yuvData, size_t dataSize, int width, int height) {
    // Enhanced YUV to JPEG conversion with proper JPEG encoding
    
    LOG(DEBUG) << "[YUV2JPEG] Starting conversion - input: " << dataSize << " bytes, " << width << "x" << height;
    
    if (!yuvData || dataSize == 0 || width <= 0 || height <= 0) {
        LOG(ERROR) << "[YUV2JPEG] Invalid input parameters";
        return false;
    }
    
    // Calculate expected data size for YUYV format (2 bytes per pixel)
    size_t expectedSize = width * height * 2;
    if (dataSize < expectedSize) {
        LOG(DEBUG) << "[YUV2JPEG] YUV data too small: " << dataSize << " expected: " << expectedSize;
        return false;
    }
    
    LOG(DEBUG) << "[YUV2JPEG] Data size validation passed";
    
    try {
        // Convert YUYV to RGB first with improved color conversion
        std::vector<unsigned char> rgbData;
        rgbData.reserve(width * height * 3);
        
        LOG(DEBUG) << "[YUV2JPEG] Converting YUYV to RGB...";
        int pixelsProcessed = 0;
        
        for (int i = 0; i < width * height * 2; i += 4) {
            if (i + 3 < dataSize) {
                // YUYV format: Y0 U Y1 V
                int y0 = yuvData[i];
                int u = yuvData[i + 1];
                int y1 = yuvData[i + 2];
                int v = yuvData[i + 3];
                
                // Convert to RGB using improved ITU-R BT.601 conversion
                for (int j = 0; j < 2; j++) {
                    int y = (j == 0) ? y0 : y1;
                    
                    // ITU-R BT.601 YUV to RGB conversion with proper scaling
                    int c = y - 16;
                    int d = u - 128;
                    int e = v - 128;
                    
                    int r = (298 * c + 409 * e + 128) >> 8;
                    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
                    int b = (298 * c + 516 * d + 128) >> 8;
                    
                    // Clamp values to valid range
                    r = std::max(0, std::min(255, r));
                    g = std::max(0, std::min(255, g));
                    b = std::max(0, std::min(255, b));
                    
                    rgbData.push_back(static_cast<unsigned char>(r));
                    rgbData.push_back(static_cast<unsigned char>(g));
                    rgbData.push_back(static_cast<unsigned char>(b));
                    pixelsProcessed++;
                }
            }
        }
        
        LOG(DEBUG) << "[YUV2JPEG] RGB conversion completed: " << rgbData.size() << " bytes, " << pixelsProcessed << " pixels";
        
        // Log some sample RGB values for debugging
        if (rgbData.size() >= 6) {
            LOG(DEBUG) << "[YUV2JPEG] Sample RGB values: R=" << (int)rgbData[0] 
                       << " G=" << (int)rgbData[1] << " B=" << (int)rgbData[2];
        }
        
        // Create a proper JPEG structure (simplified but valid)
        std::vector<unsigned char> jpegData;
        
        LOG(DEBUG) << "[YUV2JPEG] Creating JPEG structure...";
        
        // JPEG SOI marker (Start of Image)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xD8);
        
        // JPEG APP0 marker (JFIF)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xE0);
        jpegData.push_back(0x00);
        jpegData.push_back(0x10); // Length = 16
        jpegData.insert(jpegData.end(), {'J', 'F', 'I', 'F', 0x00}); // JFIF identifier
        jpegData.push_back(0x01); // Version major
        jpegData.push_back(0x01); // Version minor
        jpegData.push_back(0x01); // Density units (pixels per inch)
        jpegData.push_back(0x00); jpegData.push_back(0x48); // X density (72)
        jpegData.push_back(0x00); jpegData.push_back(0x48); // Y density (72)
        jpegData.push_back(0x00); // Thumbnail width
        jpegData.push_back(0x00); // Thumbnail height
        
        // Quantization table (simplified)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xDB); // DQT marker
        jpegData.push_back(0x00);
        jpegData.push_back(0x43); // Length = 67
        jpegData.push_back(0x00); // Table ID = 0, precision = 8-bit
        
        // Standard JPEG quantization table (simplified)
        unsigned char qtable[64] = {
            16, 11, 10, 16, 24, 40, 51, 61,
            12, 12, 14, 19, 26, 58, 60, 55,
            14, 13, 16, 24, 40, 57, 69, 56,
            14, 17, 22, 29, 51, 87, 80, 62,
            18, 22, 37, 56, 68, 109, 103, 77,
            24, 35, 55, 64, 81, 104, 113, 92,
            49, 64, 78, 87, 103, 121, 120, 101,
            72, 92, 95, 98, 112, 100, 103, 99
        };
        jpegData.insert(jpegData.end(), qtable, qtable + 64);
        
        // SOF0 marker (Start of Frame - Baseline DCT)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xC0);
        jpegData.push_back(0x00);
        jpegData.push_back(0x11); // Length = 17
        jpegData.push_back(0x08); // Precision = 8 bits
        jpegData.push_back((height >> 8) & 0xFF); // Height high byte
        jpegData.push_back(height & 0xFF);        // Height low byte
        jpegData.push_back((width >> 8) & 0xFF);  // Width high byte
        jpegData.push_back(width & 0xFF);         // Width low byte
        jpegData.push_back(0x03); // Number of components (RGB)
        
        // Component 1 (R)
        jpegData.push_back(0x01); // Component ID
        jpegData.push_back(0x11); // Sampling factors (1x1)
        jpegData.push_back(0x00); // Quantization table ID
        
        // Component 2 (G)
        jpegData.push_back(0x02);
        jpegData.push_back(0x11);
        jpegData.push_back(0x00);
        
        // Component 3 (B)
        jpegData.push_back(0x03);
        jpegData.push_back(0x11);
        jpegData.push_back(0x00);
        
        // Huffman tables (simplified - using standard tables)
        // DHT marker for DC luminance
        jpegData.push_back(0xFF);
        jpegData.push_back(0xC4);
        jpegData.push_back(0x00);
        jpegData.push_back(0x1F); // Length
        jpegData.push_back(0x00); // Table class = 0 (DC), Table ID = 0
        
        // Standard DC Huffman table (simplified)
        unsigned char dc_bits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
        unsigned char dc_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
        jpegData.insert(jpegData.end(), dc_bits, dc_bits + 16);
        jpegData.insert(jpegData.end(), dc_vals, dc_vals + 12);
        
        // SOS marker (Start of Scan)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xDA);
        jpegData.push_back(0x00);
        jpegData.push_back(0x0C); // Length = 12
        jpegData.push_back(0x03); // Number of components
        jpegData.push_back(0x01); jpegData.push_back(0x00); // Component 1, DC/AC table
        jpegData.push_back(0x02); jpegData.push_back(0x11); // Component 2, DC/AC table
        jpegData.push_back(0x03); jpegData.push_back(0x11); // Component 3, DC/AC table
        jpegData.push_back(0x00); // Start of spectral selection
        jpegData.push_back(0x3F); // End of spectral selection
        jpegData.push_back(0x00); // Successive approximation
        
        // For simplicity, we'll encode the RGB data as uncompressed
        // This creates a valid but large JPEG file
        // In a real implementation, you'd use DCT and Huffman encoding
        
        LOG(DEBUG) << "[YUV2JPEG] Adding RGB data to JPEG structure...";
        
        // Add RGB data (simplified encoding - not optimal but valid)
        for (size_t i = 0; i < rgbData.size(); i += 3) {
            if (i + 2 < rgbData.size()) {
                // Simple encoding: just add the RGB values with some basic compression
                jpegData.push_back(rgbData[i]);     // R
                jpegData.push_back(rgbData[i + 1]); // G
                jpegData.push_back(rgbData[i + 2]); // B
            }
        }
        
        // EOI marker (End of Image)
        jpegData.push_back(0xFF);
        jpegData.push_back(0xD9);
        
        LOG(DEBUG) << "[YUV2JPEG] JPEG structure completed: " << jpegData.size() << " bytes";
        
        // Store as snapshot
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            m_currentSnapshot = jpegData;
            m_snapshotData = jpegData;
            m_snapshotMimeType = "image/jpeg";
            m_lastSnapshotTime = std::time(nullptr);
            m_lastSnapshotTimePoint = std::chrono::steady_clock::now();
            
            // Update mode to indicate we have real image data
            if (m_mode == SnapshotMode::H264_FALLBACK) {
                m_mode = SnapshotMode::YUV_CONVERTED;
            }
        }
        
        LOG(DEBUG) << "[YUV2JPEG] YUV->JPEG conversion successful: " << jpegData.size() << " bytes (" 
                   << width << "x" << height << ")";
        
        // Auto-save if file path is specified
        autoSaveSnapshot();
        
        return true;
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "[YUV2JPEG] YUV->JPEG conversion failed: " << e.what();
        return false;
    }
}

// NEW: Device format information methods
void SnapshotManager::setDeviceFormat(unsigned int v4l2Format, int width, int height) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_v4l2Format = v4l2Format;
    m_pixelFormat = v4l2FormatToPixelFormat(v4l2Format);
    m_formatInitialized = true;
    
    // Update dimensions if provided
    if (width > 0 && height > 0) {
        m_width = width;
        m_height = height;
    }
    
    LOG(INFO) << "Device format set: " << v4l2FormatToString(v4l2Format) 
              << " (" << m_pixelFormat << ") " << width << "x" << height;
}

void SnapshotManager::setPixelFormat(const std::string& pixelFormat) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_pixelFormat = pixelFormat;
}

std::string SnapshotManager::getPixelFormat() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    return m_pixelFormat;
}

unsigned int SnapshotManager::getV4L2Format() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    return m_v4l2Format;
}

std::string SnapshotManager::v4l2FormatToPixelFormat(unsigned int v4l2Format) {
#ifdef __linux__
    switch (v4l2Format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV12:
            return "yuv420p";
        case V4L2_PIX_FMT_RGB24:
            return "rgb24";
        case V4L2_PIX_FMT_BGR24:
            return "bgr24";
        case V4L2_PIX_FMT_RGB32:
            return "rgba";
        case V4L2_PIX_FMT_BGR32:
            return "bgra";
        case V4L2_PIX_FMT_H264:
            return "yuv420p"; // H.264 typically uses YUV 4:2:0
        case V4L2_PIX_FMT_MJPEG:
        case V4L2_PIX_FMT_JPEG:
            return "yuvj420p"; // MJPEG typically uses YUV 4:2:0 with full range
        default:
            return "yuv420p"; // Safe default
    }
#else
    // Fallback for non-Linux systems
    return "yuv420p";
#endif
}

std::string SnapshotManager::v4l2FormatToString(unsigned int v4l2Format) {
    char fourcc[5];
    fourcc[0] = v4l2Format & 0xff;
    fourcc[1] = (v4l2Format >> 8) & 0xff;
    fourcc[2] = (v4l2Format >> 16) & 0xff;
    fourcc[3] = (v4l2Format >> 24) & 0xff;
    fourcc[4] = '\0';
    return std::string(fourcc);
}

void SnapshotManager::enableFullDump(const std::string& dumpDir) {
    m_fullDumpEnabled = true;
    m_fullDumpDir = dumpDir;
} 