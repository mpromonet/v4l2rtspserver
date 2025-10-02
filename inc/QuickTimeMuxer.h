/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** QuickTimeMuxer.h
** 
** Wrapper for live555 QuickTimeFileSink for MP4 recording
**
** -------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

class QuickTimeMuxer {
public:
    QuickTimeMuxer();
    ~QuickTimeMuxer() noexcept;
    
    // Initialize MP4 file with parameters
    bool initialize(int fd, const std::string& sps, const std::string& pps, int width, int height, int fps = 30);
    
    // Add H264 frame data
    bool addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame);
    
    // Finalize MP4 file
    bool finalize();
    
    // Check if muxer is initialized
    bool isInitialized() const { return m_initialized; }
    
    // Get file descriptor for sync operations
    int getFileDescriptor() const { return m_fd; }
    
    // Static method for creating MP4 snapshot in memory (for SnapshotManager)
    static std::vector<uint8_t> createMP4Snapshot(const unsigned char* h264Data, size_t dataSize,
                                                   const std::string& sps, const std::string& pps,
                                                   int width, int height, int fps = 30);
    
    // Helper static methods for NAL analysis
    static std::string getNALTypeName(uint8_t nalType);
    static std::string getCurrentTimestamp();
    
private:
    bool m_initialized;
    int m_fd;
    std::string m_sps;
    std::string m_pps;
    int m_width;
    int m_height;
    int m_fps;
    
    // File position tracking
    size_t m_mdatStartPos;
    size_t m_moovStartPos;
    size_t m_currentPos;
    
    // Frame counting
    uint32_t m_frameCount;
    uint32_t m_keyFrameCount;
    
    // Frame metadata for streaming
    struct FrameInfo {
        size_t offset;
        size_t size;
        bool isKeyFrame;
    };
    std::vector<FrameInfo> m_frames;
    
    // Helper methods
    void writeToFile(const void* data, size_t size);
    bool writeMP4Header();
    bool writeMoovBox();
    
    // Static helper methods - based on live555 QuickTimeFileSink structure
    static std::vector<uint8_t> createFtypBox();
    static std::vector<uint8_t> createMinimalMoovBox();
    static std::vector<uint8_t> createVideoTrackMoovBox(const std::vector<uint8_t>& sps, 
                                                       const std::vector<uint8_t>& pps, 
                                                       int width, int height, int fps, 
                                                       uint32_t frameCount);
    static std::vector<uint8_t> createTrakBox(const std::vector<uint8_t>& sps,
                                             const std::vector<uint8_t>& pps,
                                             int width, int height,
                                             uint32_t timescale, uint32_t duration,
                                             uint32_t frameCount);
    static std::vector<uint8_t> createMdiaBox(const std::vector<uint8_t>& sps,
                                             const std::vector<uint8_t>& pps,
                                             int width, int height,
                                             uint32_t timescale, uint32_t duration,
                                             uint32_t frameCount);
    static std::vector<uint8_t> createMinfBox(const std::vector<uint8_t>& sps,
                                             const std::vector<uint8_t>& pps,
                                             int width, int height,
                                             uint32_t frameCount);
    static std::vector<uint8_t> createStblBox(const std::vector<uint8_t>& sps,
                                             const std::vector<uint8_t>& pps,
                                             int width, int height,
                                             uint32_t frameCount);
    static std::vector<uint8_t> createMdatBox(const std::vector<uint8_t>& frameData);
    static void write32(std::vector<uint8_t>& vec, uint32_t value);
    static void write16(std::vector<uint8_t>& vec, uint16_t value);
    static void write8(std::vector<uint8_t>& vec, uint8_t value);
};
