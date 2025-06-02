/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManager.h
** 
** Real Image Snapshot Manager for v4l2rtspserver
**
** -------------------------------------------------------------------------*/

#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <memory>
#include <ctime>
#include <chrono>

#ifdef __linux__
#include <linux/videodev2.h>
#endif

// Forward declaration
struct V4L2DeviceParameters;

enum class SnapshotMode {
    DISABLED,
    MJPEG_STREAM,     // Real JPEG snapshots from MJPEG stream
    H264_FALLBACK,    // MP4 snapshots with cached H264 frames
    H264_MP4,         // Mini MP4 snapshots with H264 keyframes
    YUV_CONVERTED     // Real images converted from YUV data
};

class SnapshotManager {
public:
    static SnapshotManager& getInstance() {
        static SnapshotManager instance;
        return instance;
    }

    // Configuration
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setFrameDimensions(int width, int height);
    void setSnapshotResolution(int width, int height);
    void setFilePath(const std::string& filePath) { m_filePath = filePath; }
    void setSaveInterval(int intervalSeconds);  // Validates range 1-60 seconds
    
    // Device format information
    void setDeviceFormat(unsigned int v4l2Format, int width, int height);
    void setPixelFormat(const std::string& pixelFormat);
    std::string getPixelFormat() const;
    unsigned int getV4L2Format() const;
    
    // Initialization
    bool initialize(int width, int height);
    
    // Frame processing (called by existing video sources)
    void processMJPEGFrame(const unsigned char* jpegData, size_t dataSize);
    void processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height);
    void processH264KeyframeWithSPS(const unsigned char* h264Data, size_t dataSize, 
                                   const std::string& sps, const std::string& pps, 
                                   int width, int height);
    void processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height);
    
    // Snapshot retrieval
    bool getSnapshot(std::vector<unsigned char>& jpegData);
    std::string getSnapshotMimeType() const;
    
    // File operations
    bool saveSnapshotToFile();
    bool saveSnapshotToFile(const std::string& filePath);
    
    // Status
    SnapshotMode getMode() const { return m_mode; }
    std::string getModeDescription() const;
    bool hasRecentSnapshot() const;
    
    // Pixel format conversion helpers
    std::string v4l2FormatToPixelFormat(unsigned int v4l2Format);
    std::string v4l2FormatToString(unsigned int v4l2Format);

    // Enhanced dumping methods
    static void dumpDeviceInfo(const std::string& device, int width, int height, 
                              int pixelFormat, int fps);
#ifdef __linux__
    static void dumpV4L2Capabilities(const v4l2_capability& caps);
    static void dumpPixelFormat(const v4l2_format& fmt);
#endif
    static void dumpH264Parameters(const std::vector<uint8_t>& sps, 
                                  const std::vector<uint8_t>& pps);
    static void dumpFrameData(const std::vector<uint8_t>& frameData, 
                             const std::string& frameType);
    static void dumpSEIData(const std::vector<uint8_t>& seiData);
    static void dumpStreamStatistics(int total, int i, int p, int b);
    static std::string getDumpDirectory();

    void enableFullDump(const std::string& dumpDir);

private:
    SnapshotManager();
    ~SnapshotManager();
    SnapshotManager(const SnapshotManager&) = delete;
    SnapshotManager& operator=(const SnapshotManager&) = delete;
    
    // Snapshot creation
    void createH264Snapshot(const unsigned char* h264Data, size_t h264Size, 
                           int width, int height,
                           const std::string& sps = "", const std::string& pps = "");
    void autoSaveSnapshot();
    bool convertYUVToJPEG(const unsigned char* yuvData, size_t dataSize, int width, int height);
    
    // Dynamic NAL unit extraction (inspired by go2rtc)
    std::vector<uint8_t> findNALUnit(const uint8_t* data, size_t size, uint8_t nalType);
    
    // Members
    bool m_enabled;
    SnapshotMode m_mode;
    int m_width;
    int m_height;
    int m_snapshotWidth;
    int m_snapshotHeight;
    
    // Device format information
    unsigned int m_v4l2Format;
    std::string m_pixelFormat;
    bool m_formatInitialized;
    
    // Thread safety
    mutable std::mutex m_snapshotMutex;
    std::vector<unsigned char> m_currentSnapshot;
    std::time_t m_lastSnapshotTime;
    
    // Snapshot data storage
    std::vector<unsigned char> m_snapshotData;
    std::string m_snapshotMimeType;
    std::chrono::steady_clock::time_point m_lastSnapshotTimePoint;
    
    // File operations
    std::string m_filePath;
    int m_saveInterval;
    std::time_t m_lastSaveTime;
    
    // H264 frame cache for snapshots
    std::vector<unsigned char> m_lastH264Frame;
    std::string m_lastSPS;
    std::string m_lastPPS;
    int m_lastFrameWidth;
    int m_lastFrameHeight;

    bool m_fullDumpEnabled = false;
    std::string m_fullDumpDir;
};
