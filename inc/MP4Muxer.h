/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MP4Muxer.h
** 
** MP4 muxer for efficient H264 video recording
**
** -------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

// Structure for storing frame information
struct FrameInfo {
    size_t offset;      // position in file
    size_t size;        // frame size (including length prefix)
    bool isKeyFrame;    // is keyframe
};

class MP4Muxer {
public:
    MP4Muxer();
    ~MP4Muxer() noexcept;
    
    // Initialize MP4 file with SPS/PPS and dimensions
    bool initialize(int fd, const std::string& sps, const std::string& pps, int width, int height, int fps = 30);
    
    // Add H264 frame (will handle keyframes and regular frames)
    bool addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame);
    
    // Finalize MP4 file (update metadata)
    bool finalize();
    
    // Check if muxer is initialized
    bool isInitialized() const { return m_initialized; }
    
    // Get file descriptor for sync operations
    int getFileDescriptor() const { return m_fd; }
    
    // Static method for creating MP4 snapshot in memory (for SnapshotManager)
    static std::vector<uint8_t> createMP4Snapshot(const unsigned char* h264Data, size_t dataSize,
                                                   const std::string& sps, const std::string& pps,
                                                   int width, int height, int fps = 30);
    
    // Static debug method for H264 data analysis
    static void debugDumpH264Data(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps, 
                                  const std::vector<uint8_t>& h264Data, int width, int height);
    
    // Helper static methods for NAL analysis
    static std::string getNALTypeName(uint8_t nalType);
    static std::string getCurrentTimestamp();
    
private:
    int m_fd;
    bool m_initialized;
    std::string m_sps;
    std::string m_pps;
    int m_width;
    int m_height;
    int m_fps;  // Dynamic FPS parameter
    
    // Track file positions for metadata updates
    size_t m_mdatStartPos;
    size_t m_moovStartPos;  // Position of placeholder moov box
    size_t m_currentPos;
    
    // Frame counting
    uint32_t m_frameCount;
    uint32_t m_keyFrameCount;
    
    // Frame metadata for streaming
    std::vector<FrameInfo> m_frames;
    
    // Buffering system for disk write optimization
    std::vector<uint8_t> m_writeBuffer;      // In-memory buffer for frame data
    size_t m_bufferMaxSize;                   // Maximum buffer size (1MB default)
    std::chrono::steady_clock::time_point m_lastFlushTime;  // Last disk flush time
    uint32_t m_flushIntervalMs;               // Minimum interval between flushes (1000ms default)
    
    // Helper methods from SnapshotManager
    void write32(std::vector<uint8_t>& vec, uint32_t value);
    void write16(std::vector<uint8_t>& vec, uint16_t value);
    void write8(std::vector<uint8_t>& vec, uint8_t value);
    void writeToFile(const void* data, size_t size);
    
    // Buffering management
    void writeToBuffer(const void* data, size_t size);
    void flushBufferToDisk(bool force = false);
    bool shouldFlushBuffer(bool isKeyFrame);
    
    // MP4 structure creation helpers (refactored to avoid duplication)
    static std::vector<uint8_t> createFtypBox();
    static std::vector<uint8_t> createMinimalMoovBox();
    static std::vector<uint8_t> createVideoTrackMoovBox(const std::string& sps, const std::string& pps, 
                                                       int width, int height, int fps, uint32_t frameCount);
    static std::vector<uint8_t> createMdatBox(const std::string& sps, const std::string& pps, 
                                              const unsigned char* h264Data, size_t dataSize);
    
    // Helper methods for MP4 box creation
    static std::vector<uint8_t> createAvcCBox(const std::string& sps, const std::string& pps);
    static std::vector<uint8_t> createAvc1Box(const std::string& sps, const std::string& pps, int width, int height);
    static std::vector<uint8_t> createSingleFrameMoovBox(const std::string& sps, const std::string& pps, 
                                                         int width, int height, int fps, uint32_t mdatOffset);
    
    // Create proper moov box for multiple frames
    std::vector<uint8_t> createMultiFrameMoovBox();
    
    // MP4 structure creation (moved from SnapshotManager)
    bool writeMP4Header();
    std::pair<int, int> parseSPSDimensions(const std::string& sps);
};
