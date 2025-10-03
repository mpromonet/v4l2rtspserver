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
#include <errno.h>
#include <chrono>
#include <arpa/inet.h>

// Universal MP4 box builder helper
class BoxBuilder {
    std::vector<uint8_t> data;
    
public:
    BoxBuilder& add32(uint32_t value) {
        data.push_back((value >> 24) & 0xFF);
        data.push_back((value >> 16) & 0xFF);
        data.push_back((value >> 8) & 0xFF);
        data.push_back(value & 0xFF);
        return *this;
    }
    
    BoxBuilder& add16(uint16_t value) {
        data.push_back((value >> 8) & 0xFF);
        data.push_back(value & 0xFF);
        return *this;
    }
    
    BoxBuilder& add8(uint8_t value) {
        data.push_back(value);
        return *this;
    }
    
    BoxBuilder& addBytes(const void* bytes, size_t size) {
        if (!bytes || size == 0) return *this;
        const uint8_t* ptr = static_cast<const uint8_t*>(bytes);
        data.insert(data.end(), ptr, ptr + size);
        return *this;
    }
    
    BoxBuilder& addString(const char* str) {
        if (!str) return *this;
        return addBytes(str, strlen(str));
    }
    
    BoxBuilder& addZeros(size_t count) {
        data.insert(data.end(), count, 0);
        return *this;
    }
    
    std::vector<uint8_t> build(const char* type) {
        std::vector<uint8_t> result;
        uint32_t size = data.size() + 8;
        result.push_back((size >> 24) & 0xFF);
        result.push_back((size >> 16) & 0xFF);
        result.push_back((size >> 8) & 0xFF);
        result.push_back(size & 0xFF);
        result.insert(result.end(), type, type + 4);
        result.insert(result.end(), data.begin(), data.end());
        return result;
    }
    
    const std::vector<uint8_t>& getData() const { return data; }
};

QuickTimeMuxer::QuickTimeMuxer() 
    : m_initialized(false), m_fd(-1), m_width(0), m_height(0), m_fps(30),
      m_mdatStartPos(0), m_currentPos(0),
      m_frameCount(0), m_keyFrameCount(0),
      m_bufferMaxSize(1024 * 1024), m_flushIntervalMs(1000) {  // 1MB buffer, 1 second interval
    m_writeBuffer.reserve(m_bufferMaxSize);
    m_lastFlushTime = std::chrono::steady_clock::now();
}

QuickTimeMuxer::~QuickTimeMuxer() noexcept {
    try {
        if (m_initialized) {
            flushBufferToDisk(true); // Flush buffer before finalize
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
    
    // NOTE: For recording, SPS/PPS are NOT prepended to frames in mdat
    // They are only stored in avcC box in moov (standard MP4 structure)
    // Only for snapshots (single frame), SPS/PPS are included in mdat via createMP4Snapshot
    
    // IMPORTANT: Input h264Data is already a clean NAL unit WITHOUT start codes
    // (extracted from V4L2 stream by H264_V4l2DeviceSource)
    // We just need to add 4-byte length prefix for MP4 format
    
    uint32_t frameSize = static_cast<uint32_t>(dataSize);
    
    // Write 4-byte length prefix in big-endian format (MP4 standard)
    uint8_t lenBytes[4];
    lenBytes[0] = (frameSize >> 24) & 0xFF;
    lenBytes[1] = (frameSize >> 16) & 0xFF;
    lenBytes[2] = (frameSize >> 8) & 0xFF;
    lenBytes[3] = frameSize & 0xFF;
    
    writeToFile(lenBytes, 4);
    writeToFile(h264Data, dataSize);
    
    // Save frame metadata
    FrameInfo frameInfo;
    frameInfo.size = dataSize + 4; // NAL data + 4-byte length prefix
    frameInfo.isKeyFrame = isKeyFrame;
    frameInfo.offset = m_currentPos - frameInfo.size;
    m_frames.push_back(frameInfo);
    
    m_frameCount++;
    if (isKeyFrame) {
        m_keyFrameCount++;
    }
    
    // Check if we should flush buffer to disk (on keyframes at intervals)
    if (shouldFlushBuffer(isKeyFrame)) {
        flushBufferToDisk(false); // Regular scheduled flush (no fsync)
    }
    
    LOG(DEBUG) << "[QuickTimeMuxer] Added frame " << m_frameCount << " (" << dataSize << " bytes" 
               << (isKeyFrame ? ", keyframe" : "") << ") at offset " << frameInfo.offset;
    
    return true;
}

bool QuickTimeMuxer::finalize() {
    if (!m_initialized) {
        return false;
    }
    
    // CRITICAL: Flush buffer before finalizing
    flushBufferToDisk(true);
    
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
    
    // Write mdat box header (media data will follow)
    // Note: We'll write moov AFTER mdat (at the end) as per live555 approach
    m_mdatStartPos = m_currentPos;
    uint32_t mdatSize = 0; // Will be updated later
    writeToFile(&mdatSize, 4);
    writeToFile("mdat", 4);
    
    return true;
}

bool QuickTimeMuxer::writeMoovBox() {
    // CRITICAL: Save current position BEFORE any lseek operations
    // because lseek will change file pointer and invalidate m_currentPos
    off_t actualDataEnd = m_currentPos;
    
    // Calculate mdat size (from mdat start to current position)
    size_t mdatDataSize = actualDataEnd - m_mdatStartPos - 8; // -8 for size+type
    size_t mdatTotalSize = mdatDataSize + 8;
    
    // Update mdat size at the beginning
    if (lseek(m_fd, m_mdatStartPos, SEEK_SET) == -1) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to seek to mdat position";
        return false;
    }
    
    uint32_t mdatSizeBE = htonl(static_cast<uint32_t>(mdatTotalSize));
    ssize_t written = write(m_fd, &mdatSizeBE, 4);
    if (written != 4) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to write mdat size";
        return false;
    }
    
    // Calculate where moov should be written (right after mdat)
    // Use the saved position, not m_currentPos (which may be corrupted by lseek/write)
    off_t moovStart = m_mdatStartPos + mdatTotalSize;
    
    // Seek to the calculated position to write moov
    if (lseek(m_fd, moovStart, SEEK_SET) == -1) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to seek to moov position";
        return false;
    }
    
    // Create and write moov box at the end (live555 style)
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(m_sps.begin(), m_sps.end()),
        std::vector<uint8_t>(m_pps.begin(), m_pps.end()),
        m_width, m_height, m_fps, m_frameCount
    );
    
    // Update stsz entry_sizes with actual frame sizes from m_frames
    // Find 'stsz' box and update entries
    bool stszFound = false;
    for (size_t i = 0; i + 20 + m_frames.size() * 4 <= moovBox.size(); i++) {
        if (moovBox[i] == 0x73 && moovBox[i+1] == 0x74 && 
            moovBox[i+2] == 0x73 && moovBox[i+3] == 0x7A) {
            // Found 'stsz', update entries
            size_t entriesStart = i + 16; // After 'stsz' + version/flags + sample_size + sample_count
            
            for (size_t j = 0; j < m_frames.size(); j++) {
                size_t entryPos = entriesStart + j * 4;
                if (entryPos + 4 <= moovBox.size()) {
                    uint32_t frameSize = m_frames[j].size;
                    moovBox[entryPos]   = (frameSize >> 24) & 0xFF;
                    moovBox[entryPos+1] = (frameSize >> 16) & 0xFF;
                    moovBox[entryPos+2] = (frameSize >> 8) & 0xFF;
                    moovBox[entryPos+3] = frameSize & 0xFF;
                }
            }
            
            stszFound = true;
            LOG(DEBUG) << "[QuickTimeMuxer] Updated stsz with " << m_frames.size() << " frame sizes";
            break;
        }
    }
    
    if (!stszFound) {
        LOG(WARN) << "[QuickTimeMuxer] Could not find stsz box in moov to update frame sizes!";
    }
    
    // Fix stss (sync samples) to contain only actual keyframes, not all frames
    // Find 'stss' box and rebuild with only keyframe indices
    bool stssFound = false;
    for (size_t i = 0; i + 16 <= moovBox.size(); i++) {
        if (moovBox[i] == 0x73 && moovBox[i+1] == 0x74 && 
            moovBox[i+2] == 0x73 && moovBox[i+3] == 0x73) {
            // Found 'stss'
            // Count actual keyframes
            std::vector<uint32_t> keyframeIndices;
            for (size_t j = 0; j < m_frames.size(); j++) {
                if (m_frames[j].isKeyFrame) {
                    keyframeIndices.push_back(j + 1); // 1-based index
                }
            }
            
            // Rebuild stss box with correct keyframe count
            size_t oldStssSize = (moovBox[i-4] << 24) | (moovBox[i-3] << 16) | 
                                  (moovBox[i-2] << 8) | moovBox[i-1];
            size_t newStssSize = 16 + keyframeIndices.size() * 4;
            
            // Update size
            moovBox[i-4] = (newStssSize >> 24) & 0xFF;
            moovBox[i-3] = (newStssSize >> 16) & 0xFF;
            moovBox[i-2] = (newStssSize >> 8) & 0xFF;
            moovBox[i-1] = newStssSize & 0xFF;
            
            // Update entry count
            size_t entryCountPos = i + 8; // After 'stss' + version/flags
            moovBox[entryCountPos]   = (keyframeIndices.size() >> 24) & 0xFF;
            moovBox[entryCountPos+1] = (keyframeIndices.size() >> 16) & 0xFF;
            moovBox[entryCountPos+2] = (keyframeIndices.size() >> 8) & 0xFF;
            moovBox[entryCountPos+3] = keyframeIndices.size() & 0xFF;
            
            // Write keyframe indices
            size_t entriesStart = i + 12;
            for (size_t j = 0; j < keyframeIndices.size(); j++) {
                size_t entryPos = entriesStart + j * 4;
                if (entryPos + 4 <= moovBox.size()) {
                    uint32_t idx = keyframeIndices[j];
                    moovBox[entryPos]   = (idx >> 24) & 0xFF;
                    moovBox[entryPos+1] = (idx >> 16) & 0xFF;
                    moovBox[entryPos+2] = (idx >> 8) & 0xFF;
                    moovBox[entryPos+3] = idx & 0xFF;
                }
            }
            
            // If stss is now smaller, we need to adjust container sizes
            if (newStssSize < oldStssSize) {
                size_t sizeDiff = oldStssSize - newStssSize;
                
                // Shift remaining data
                size_t stssEnd = i - 4 + oldStssSize;
                size_t newStssEnd = i - 4 + newStssSize;
                size_t remainingSize = moovBox.size() - stssEnd;
                memmove(&moovBox[newStssEnd], &moovBox[stssEnd], remainingSize);
                moovBox.resize(moovBox.size() - sizeDiff);
                
                // Update moov size (top level)
                uint32_t moovSize = moovBox.size();
                moovBox[0] = (moovSize >> 24) & 0xFF;
                moovBox[1] = (moovSize >> 16) & 0xFF;
                moovBox[2] = (moovSize >> 8) & 0xFF;
                moovBox[3] = moovSize & 0xFF;
                
                // CRITICAL: Also update parent containers (trak, mdia, minf, stbl)
                // that contain stss and their sizes are now incorrect
                // We need to find and update each parent's size field
                
                // Find trak (should be after mvhd, around offset 8+108=116)
                for (size_t j = 8; j < 200 && j + 8 <= moovBox.size(); j++) {
                    if (moovBox[j] == 0x74 && moovBox[j+1] == 0x72 &&
                        moovBox[j+2] == 0x61 && moovBox[j+3] == 0x6B) {
                        // Found 'trak', update its size (4 bytes before)
                        uint32_t trakSize = (moovBox[j-4] << 24) | (moovBox[j-3] << 16) |
                                           (moovBox[j-2] << 8) | moovBox[j-1];
                        trakSize -= sizeDiff;
                        moovBox[j-4] = (trakSize >> 24) & 0xFF;
                        moovBox[j-3] = (trakSize >> 16) & 0xFF;
                        moovBox[j-2] = (trakSize >> 8) & 0xFF;
                        moovBox[j-1] = trakSize & 0xFF;
                        LOG(DEBUG) << "[QuickTimeMuxer] Updated trak size: " << trakSize;
                        break;
                    }
                }
                
                // Find mdia, minf, stbl (nested inside trak) and update their sizes
                std::vector<std::string> containers = {"mdia", "minf", "stbl"};
                for (const auto& containerName : containers) {
                    for (size_t j = 116; j < moovBox.size() - 8; j++) {
                        if (moovBox[j] == containerName[0] && moovBox[j+1] == containerName[1] &&
                            moovBox[j+2] == containerName[2] && moovBox[j+3] == containerName[3]) {
                            // Found container, update its size (4 bytes before)
                            uint32_t containerSize = (moovBox[j-4] << 24) | (moovBox[j-3] << 16) |
                                                    (moovBox[j-2] << 8) | moovBox[j-1];
                            containerSize -= sizeDiff;
                            moovBox[j-4] = (containerSize >> 24) & 0xFF;
                            moovBox[j-3] = (containerSize >> 16) & 0xFF;
                            moovBox[j-2] = (containerSize >> 8) & 0xFF;
                            moovBox[j-1] = containerSize & 0xFF;
                            LOG(DEBUG) << "[QuickTimeMuxer] Updated " << containerName << " size: " << containerSize;
                            break;
                        }
                    }
                }
            }
            
            stssFound = true;
            LOG(DEBUG) << "[QuickTimeMuxer] Fixed stss: " << keyframeIndices.size() << " keyframes";
            break;
        }
    }
    
    if (!stssFound) {
        LOG(WARN) << "[QuickTimeMuxer] Could not find stss box in moov to fix keyframes!";
    }
    
    // Fix stco (chunk offset) to point to actual mdat data position
    // stco must point to where frames start in the file (after ftyp and mdat header)
    uint32_t actualChunkOffset = m_mdatStartPos + 8; // mdat header is 8 bytes (size + 'mdat')
    
    bool stcoFound = false;
    for (size_t i = 0; i + 16 <= moovBox.size(); i++) {
        if (moovBox[i] == 0x73 && moovBox[i+1] == 0x74 && 
            moovBox[i+2] == 0x63 && moovBox[i+3] == 0x6F) {
            // Found 'stco', skip 'stco'(4) + version/flags(4) + entry_count(4) = 12 bytes
            size_t offsetPos = i + 12;
            if (offsetPos + 4 <= moovBox.size()) {
                uint32_t oldOffset = (moovBox[offsetPos] << 24) | (moovBox[offsetPos+1] << 16) | 
                                     (moovBox[offsetPos+2] << 8) | moovBox[offsetPos+3];
                moovBox[offsetPos]   = (actualChunkOffset >> 24) & 0xFF;
                moovBox[offsetPos+1] = (actualChunkOffset >> 16) & 0xFF;
                moovBox[offsetPos+2] = (actualChunkOffset >> 8) & 0xFF;
                moovBox[offsetPos+3] = actualChunkOffset & 0xFF;
                stcoFound = true;
                LOG(DEBUG) << "[QuickTimeMuxer] Fixed stco offset: 0x" << std::hex << oldOffset 
                          << " -> 0x" << actualChunkOffset << std::dec;
                break;
            }
        }
    }
    
    if (!stcoFound) {
        LOG(WARN) << "[QuickTimeMuxer] Could not find stco box in moov to fix chunk offset!";
    }
    
    // Write moov at the end of file
    written = write(m_fd, moovBox.data(), moovBox.size());
    if (written != static_cast<ssize_t>(moovBox.size())) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to write moov box: " << written << "/" << moovBox.size();
        return false;
    }
    
    // CRITICAL: Sync data to disk BEFORE truncate
    // Otherwise ftruncate may not work correctly with buffered data
    fsync(m_fd);
    
    // Truncate file to current position (remove any garbage after moov)
    off_t finalSize = moovStart + moovBox.size();
    if (ftruncate(m_fd, finalSize) == -1) {
        LOG(WARN) << "[QuickTimeMuxer] Failed to truncate file to " << finalSize << " bytes (errno: " << errno << ")";
    } else {
        LOG(DEBUG) << "[QuickTimeMuxer] Truncated file to " << finalSize << " bytes";
    }
    
    // Final sync after truncate
    fsync(m_fd);
    
    LOG(INFO) << "[QuickTimeMuxer] Wrote moov box (" << moovBox.size() << " bytes) at end, mdat size (" << mdatTotalSize << " bytes), final file size " << finalSize;
    
    return true;
}

void QuickTimeMuxer::writeToFile(const void* data, size_t size) {
    if (m_fd == -1 || !data || size == 0) {
        return;
    }
    
    // Add data to write buffer
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    m_writeBuffer.insert(m_writeBuffer.end(), bytes, bytes + size);
    m_currentPos += size;
    
    // Check if buffer is getting too large (force flush)
    if (m_writeBuffer.size() >= m_bufferMaxSize) {
        LOG(WARN) << "[QuickTimeMuxer] Buffer size limit reached (" << m_writeBuffer.size() 
                  << " bytes), forcing flush";
        flushBufferToDisk(true);
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
    
    // Prepare mdat content: SPS + PPS + Frame (each with 4-byte length prefix)
    std::vector<uint8_t> mdatContent;
    
    // Add SPS with length prefix
    if (!sps.empty()) {
        write32(mdatContent, sps.size());
        mdatContent.insert(mdatContent.end(), sps.begin(), sps.end());
    }
    
    // Add PPS with length prefix
    if (!pps.empty()) {
        write32(mdatContent, pps.size());
        mdatContent.insert(mdatContent.end(), pps.begin(), pps.end());
    }
    
    // Add frame data with length prefix
    write32(mdatContent, dataSize);
    mdatContent.insert(mdatContent.end(), h264Data, h264Data + dataSize);
    
    // Create mdat box with all data
    auto mdatBox = createMdatBox(mdatContent);
    
    uint32_t mdatOffset = ftypBox.size(); // Offset where mdat starts
    mp4Data.insert(mp4Data.end(), mdatBox.begin(), mdatBox.end());
    
    // Create moov box AFTER mdat (standard MP4 structure for streaming)
    // Note: stco offset in moov needs to point to SPS (first data in mdat)
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(sps.begin(), sps.end()),
        std::vector<uint8_t>(pps.begin(), pps.end()),
        width, height, fps, 1
    );
    
    // Fix stco offset in moov to point to actual mdat data position (after mdat header)
    uint32_t actualOffset = mdatOffset + 8; // Skip mdat header (8 bytes: size + 'mdat')
    
    bool stcoFound = false;
    for (size_t i = 0; i + 16 <= moovBox.size(); i++) {
        if (moovBox[i] == 0x73 && moovBox[i+1] == 0x74 && 
            moovBox[i+2] == 0x63 && moovBox[i+3] == 0x6F) {
            // Found 'stco', skip 'stco'(4) + version/flags(4) + entry_count(4) = 12 bytes
            size_t offsetPos = i + 12;
            if (offsetPos + 4 <= moovBox.size()) {
                uint32_t oldOffset = (moovBox[offsetPos] << 24) | (moovBox[offsetPos+1] << 16) | 
                                     (moovBox[offsetPos+2] << 8) | moovBox[offsetPos+3];
                moovBox[offsetPos]   = (actualOffset >> 24) & 0xFF;
                moovBox[offsetPos+1] = (actualOffset >> 16) & 0xFF;
                moovBox[offsetPos+2] = (actualOffset >> 8) & 0xFF;
                moovBox[offsetPos+3] = actualOffset & 0xFF;
                stcoFound = true;
                break;
            }
        }
    }
    
    if (!stcoFound) {
        LOG(WARN) << "[Snapshot] Could not find stco box in moov to fix offset!";
    }
    
    // Fix stsz entry_sizes[0] to contain actual frame size
    // Find 'stsz' box and update the first entry
    uint32_t frameSize = mdatContent.size(); // Total size of all data in mdat (SPS + PPS + Frame)
    
    bool stszFound = false;
    for (size_t i = 0; i + 24 <= moovBox.size(); i++) {
        if (moovBox[i] == 0x73 && moovBox[i+1] == 0x74 && 
            moovBox[i+2] == 0x73 && moovBox[i+3] == 0x7A) {
            // Found 'stsz', structure: 'stsz'(4) + version/flags(4) + sample_size(4) + sample_count(4) + entries...
            // Skip to first entry: 'stsz'(4) + version/flags(4) + sample_size(4) + sample_count(4) = 16 bytes
            size_t entryPos = i + 16;
            if (entryPos + 4 <= moovBox.size()) {
                uint32_t oldSize = (moovBox[entryPos] << 24) | (moovBox[entryPos+1] << 16) | 
                                   (moovBox[entryPos+2] << 8) | moovBox[entryPos+3];
                moovBox[entryPos]   = (frameSize >> 24) & 0xFF;
                moovBox[entryPos+1] = (frameSize >> 16) & 0xFF;
                moovBox[entryPos+2] = (frameSize >> 8) & 0xFF;
                moovBox[entryPos+3] = frameSize & 0xFF;
                stszFound = true;
                break;
            }
        }
    }
    
    if (!stszFound) {
        LOG(WARN) << "[Snapshot] Could not find stsz box in moov to fix entry size!";
    }
    
    mp4Data.insert(mp4Data.end(), moovBox.begin(), moovBox.end());
    
    return mp4Data;
}

std::vector<uint8_t> QuickTimeMuxer::createFtypBox() {
    return BoxBuilder()
        .add32(0x200)           // minor_version
        .addString("isom")      // major_brand
        .addString("iso2")      // compatible_brands
        .addString("avc1")
        .addString("mp41")
        .build("ftyp");
}

// createMinimalMoovBox removed - was not used

std::vector<uint8_t> QuickTimeMuxer::createVideoTrackMoovBox(const std::vector<uint8_t>& sps, 
                                                             const std::vector<uint8_t>& pps, 
                                                             int width, int height, int fps, 
                                                             uint32_t frameCount) {
    // Based on live555 QuickTimeFileSink implementation
    // This creates a complete, valid MP4 moov box with all necessary atoms
    
    std::vector<uint8_t> moov;
    uint32_t timescale = (fps > 0) ? fps * 1000 : 30000; // H.264 timescale (fps * 1000)
    uint32_t duration = frameCount * 1000; // duration in timescale units
    
    // Build mvhd (Movie Header)
    std::vector<uint8_t> mvhd;
    write32(mvhd, 0x6D766864); // 'mvhd'
    write32(mvhd, 0); // version(0) + flags(0)
    write32(mvhd, 0); // creation_time
    write32(mvhd, 0); // modification_time
    write32(mvhd, timescale); // timescale
    write32(mvhd, duration); // duration
    write32(mvhd, 0x00010000); // rate (1.0)
    write16(mvhd, 0x0100); // volume (1.0)
    write16(mvhd, 0); // reserved
    write32(mvhd, 0); write32(mvhd, 0); // reserved[2]
    // Matrix structure (identity matrix)
    write32(mvhd, 0x00010000); write32(mvhd, 0); write32(mvhd, 0);
    write32(mvhd, 0); write32(mvhd, 0x00010000); write32(mvhd, 0);
    write32(mvhd, 0); write32(mvhd, 0); write32(mvhd, 0x40000000);
    // Pre-defined
    for (int i = 0; i < 6; i++) write32(mvhd, 0);
    write32(mvhd, 2); // next_track_ID
    uint32_t mvhdSize = 4 + mvhd.size(); // size(4) + [type+content already in mvhd]
    
    // Build trak (Track)
    std::vector<uint8_t> trak = createTrakBox(sps, pps, width, height, timescale, duration, frameCount);
    
    // Assemble moov box
    // moov structure: size(4) + type(4) + mvhd (with its own size/type) + trak
    // Calculate moov size: moov_header(8) + mvhd_box(with size) + trak_box
    // mvhd needs to include its own size field (4 bytes) first
    std::vector<uint8_t> mvhdBox;
    write32(mvhdBox, mvhdSize); // mvhd size
    mvhdBox.insert(mvhdBox.end(), mvhd.begin(), mvhd.end());
    
    uint32_t moovSize = 8 + mvhdBox.size() + trak.size();
    write32(moov, moovSize); // moov size
    write32(moov, 0x6D6F6F76); // 'moov'
    
    // Append mvhd box (with size field)
    moov.insert(moov.end(), mvhdBox.begin(), mvhdBox.end());
    
    // Append trak box
    moov.insert(moov.end(), trak.begin(), trak.end());
    
    return moov;
}

std::vector<uint8_t> QuickTimeMuxer::createTrakBox(const std::vector<uint8_t>& sps,
                                                    const std::vector<uint8_t>& pps,
                                                    int width, int height,
                                                    uint32_t timescale, uint32_t duration,
                                                    uint32_t frameCount) {
    std::vector<uint8_t> trak;
    
    // Build tkhd (Track Header)
    std::vector<uint8_t> tkhd;
    write32(tkhd, 0x746B6864); // 'tkhd'
    write32(tkhd, 0x0000000F); // version(0) + flags(enabled|in_movie|in_preview)
    write32(tkhd, 0); // creation_time
    write32(tkhd, 0); // modification_time
    write32(tkhd, 1); // track_ID
    write32(tkhd, 0); // reserved
    write32(tkhd, duration); // duration
    write32(tkhd, 0); write32(tkhd, 0); // reserved[2]
    write16(tkhd, 0); // layer
    write16(tkhd, 0); // alternate_group
    write16(tkhd, 0); // volume (0 for video)
    write16(tkhd, 0); // reserved
    // Matrix
    write32(tkhd, 0x00010000); write32(tkhd, 0); write32(tkhd, 0);
    write32(tkhd, 0); write32(tkhd, 0x00010000); write32(tkhd, 0);
    write32(tkhd, 0); write32(tkhd, 0); write32(tkhd, 0x40000000);
    write32(tkhd, width << 16); // track width
    write32(tkhd, height << 16); // track height
    uint32_t tkhdSize = 4 + tkhd.size(); // size(4) + tkhd_content
    
    // Build mdia (Media)
    std::vector<uint8_t> mdia = createMdiaBox(sps, pps, width, height, timescale, duration, frameCount);
    
    // Create tkhd box with size field
    std::vector<uint8_t> tkhdBox;
    write32(tkhdBox, tkhdSize);
    tkhdBox.insert(tkhdBox.end(), tkhd.begin(), tkhd.end());
    
    // Assemble trak
    uint32_t trakSize = 8 + tkhdBox.size() + mdia.size();
    write32(trak, trakSize);
    write32(trak, 0x7472616B); // 'trak'
    trak.insert(trak.end(), tkhdBox.begin(), tkhdBox.end());
    trak.insert(trak.end(), mdia.begin(), mdia.end());
    
    return trak;
}

std::vector<uint8_t> QuickTimeMuxer::createMdiaBox(const std::vector<uint8_t>& sps,
                                                    const std::vector<uint8_t>& pps,
                                                    int width, int height,
                                                    uint32_t timescale, uint32_t duration,
                                                    uint32_t frameCount) {
    std::vector<uint8_t> mdia;
    
    // Build mdhd (Media Header)
    std::vector<uint8_t> mdhd;
    write32(mdhd, 0x6D646864); // 'mdhd'
    write32(mdhd, 0); // version + flags
    write32(mdhd, 0); // creation_time
    write32(mdhd, 0); // modification_time
    write32(mdhd, timescale); // timescale
    write32(mdhd, duration); // duration
    write16(mdhd, 0x55C4); // language (undetermined)
    write16(mdhd, 0); // pre_defined
    uint32_t mdhdSize = 4 + mdhd.size(); // size(4) + mdhd_content
    
    // Build hdlr (Handler Reference)
    std::vector<uint8_t> hdlr;
    write32(hdlr, 0x68646C72); // 'hdlr'
    write32(hdlr, 0); // version + flags
    write32(hdlr, 0); // pre_defined
    write32(hdlr, 0x76696465); // handler_type = 'vide'
    write32(hdlr, 0); write32(hdlr, 0); write32(hdlr, 0); // reserved[3]
    const char* handlerName = "VideoHandler";
    for (const char* p = handlerName; *p; p++) write8(hdlr, *p);
    write8(hdlr, 0); // null terminator
    uint32_t hdlrSize = 4 + hdlr.size(); // size(4) + hdlr_content
    
    // Build minf (Media Information)
    std::vector<uint8_t> minf = createMinfBox(sps, pps, width, height, frameCount);
    
    // Create mdhd and hdlr boxes with size fields
    std::vector<uint8_t> mdhdBox;
    write32(mdhdBox, mdhdSize);
    mdhdBox.insert(mdhdBox.end(), mdhd.begin(), mdhd.end());
    
    std::vector<uint8_t> hdlrBox;
    write32(hdlrBox, hdlrSize);
    hdlrBox.insert(hdlrBox.end(), hdlr.begin(), hdlr.end());
    
    // Assemble mdia
    uint32_t mdiaSize = 8 + mdhdBox.size() + hdlrBox.size() + minf.size();
    write32(mdia, mdiaSize);
    write32(mdia, 0x6D646961); // 'mdia'
    mdia.insert(mdia.end(), mdhdBox.begin(), mdhdBox.end());
    mdia.insert(mdia.end(), hdlrBox.begin(), hdlrBox.end());
    mdia.insert(mdia.end(), minf.begin(), minf.end());
    
    return mdia;
}

std::vector<uint8_t> QuickTimeMuxer::createMinfBox(const std::vector<uint8_t>& sps,
                                                    const std::vector<uint8_t>& pps,
                                                    int width, int height,
                                                    uint32_t frameCount) {
    std::vector<uint8_t> minf;
    
    // Build vmhd (Video Media Header)
    std::vector<uint8_t> vmhd;
    write32(vmhd, 0x766D6864); // 'vmhd'
    write32(vmhd, 0x00000001); // version + flags
    write16(vmhd, 0); // graphicsmode
    write16(vmhd, 0); write16(vmhd, 0); write16(vmhd, 0); // opcolor[3]
    uint32_t vmhdSize = 4 + vmhd.size(); // size(4) + vmhd_content
    
    // Build dinf/dref (Data Information)
    std::vector<uint8_t> dref;
    write32(dref, 0x64726566); // 'dref'
    write32(dref, 0); // version + flags
    write32(dref, 1); // entry_count
    // url entry
    write32(dref, 12); // size
    write32(dref, 0x75726C20); // 'url '
    write32(dref, 1); // version + flags (self-contained)
    uint32_t drefSize = 4 + dref.size(); // size(4) + dref_content
    
    // Create dref box with size field
    std::vector<uint8_t> drefBox;
    write32(drefBox, drefSize);
    drefBox.insert(drefBox.end(), dref.begin(), dref.end());
    
    std::vector<uint8_t> dinf;
    write32(dinf, 8 + drefBox.size());
    write32(dinf, 0x64696E66); // 'dinf'
    dinf.insert(dinf.end(), drefBox.begin(), drefBox.end());
    
    // Build stbl (Sample Table)
    std::vector<uint8_t> stbl = createStblBox(sps, pps, width, height, frameCount);
    
    // Create vmhd box with size field
    std::vector<uint8_t> vmhdBox;
    write32(vmhdBox, vmhdSize);
    vmhdBox.insert(vmhdBox.end(), vmhd.begin(), vmhd.end());
    
    // Assemble minf
    uint32_t minfSize = 8 + vmhdBox.size() + dinf.size() + stbl.size();
    write32(minf, minfSize);
    write32(minf, 0x6D696E66); // 'minf'
    minf.insert(minf.end(), vmhdBox.begin(), vmhdBox.end());
    minf.insert(minf.end(), dinf.begin(), dinf.end());
    minf.insert(minf.end(), stbl.begin(), stbl.end());
    
    return minf;
}

std::vector<uint8_t> QuickTimeMuxer::createStblBox(const std::vector<uint8_t>& sps,
                                                    const std::vector<uint8_t>& pps,
                                                    int width, int height,
                                                    uint32_t frameCount) {
    std::vector<uint8_t> stbl;
    
    // Build stsd (Sample Description)
    // avcC configuration record (content only, type and size added below)
    std::vector<uint8_t> avcC;
    write8(avcC, 1); // configurationVersion
    write8(avcC, sps.size() > 1 ? sps[1] : 0x64); // AVCProfileIndication
    write8(avcC, sps.size() > 2 ? sps[2] : 0x00); // profile_compatibility
    write8(avcC, sps.size() > 3 ? sps[3] : 0x28); // AVCLevelIndication
    write8(avcC, 0xFF); // lengthSizeMinusOne
    write8(avcC, 0xE1); // numOfSequenceParameterSets
    write16(avcC, sps.size());
    avcC.insert(avcC.end(), sps.begin(), sps.end());
    write8(avcC, 1); // numOfPictureParameterSets
    write16(avcC, pps.size());
    avcC.insert(avcC.end(), pps.begin(), pps.end());
    
    // avcC box (size + type + content)
    std::vector<uint8_t> avcCBox;
    write32(avcCBox, 8 + avcC.size()); // size
    write32(avcCBox, 0x61766343); // 'avcC'
    avcCBox.insert(avcCBox.end(), avcC.begin(), avcC.end());
    
    // Build avc1 sample entry (following old MP4Muxer.cpp structure exactly)
    std::vector<uint8_t> avc1;
    write32(avc1, 0); // size placeholder
    avc1.insert(avc1.end(), {'a', 'v', 'c', '1'});
    // reserved[6]
    for (int i = 0; i < 6; i++) write8(avc1, 0);
    write16(avc1, 1); // data_reference_index
    // pre_defined and reserved[16]
    for (int i = 0; i < 16; i++) write8(avc1, 0);
    write16(avc1, width); // width
    write16(avc1, height); // height
    write32(avc1, 0x00480000); // horizresolution
    write32(avc1, 0x00480000); // vertresolution
    write32(avc1, 0); // reserved
    write16(avc1, 1); // frame_count
    // compressorname[32]
    for (int i = 0; i < 32; i++) write8(avc1, 0);
    write16(avc1, 0x0018); // depth
    write16(avc1, 0xFFFF); // pre_defined
    
    // Add avcC box
    avc1.insert(avc1.end(), avcCBox.begin(), avcCBox.end());
    
    // Update avc1 size
    uint32_t avc1Size = avc1.size();
    avc1[0] = (avc1Size >> 24) & 0xFF;
    avc1[1] = (avc1Size >> 16) & 0xFF;
    avc1[2] = (avc1Size >> 8) & 0xFF;
    avc1[3] = avc1Size & 0xFF;
    
    // Build stsd (Sample Description box)
    std::vector<uint8_t> stsd;
    write32(stsd, 0); // size placeholder
    stsd.insert(stsd.end(), {'s', 't', 's', 'd'});
    write8(stsd, 0); // version
    write8(stsd, 0); write8(stsd, 0); write8(stsd, 0); // flags
    write32(stsd, 1); // entry_count
    stsd.insert(stsd.end(), avc1.begin(), avc1.end());
    
    // Update stsd size
    uint32_t stsdSize = stsd.size();
    stsd[0] = (stsdSize >> 24) & 0xFF;
    stsd[1] = (stsdSize >> 16) & 0xFF;
    stsd[2] = (stsdSize >> 8) & 0xFF;
    stsd[3] = stsdSize & 0xFF;
    
    // Build stts (Time-to-Sample)
    auto stts = BoxBuilder()
        .add32(0).add32(1)          // version/flags, entry_count
        .add32(frameCount)          // sample_count
        .add32(1000)                // sample_delta
        .build("stts");
    
    // Build stss (Sync Sample)
    std::vector<uint8_t> stss;
    uint32_t stssSize = 16 + frameCount * 4; // header(16) + entries(frameCount*4)
    write32(stss, stssSize);
    stss.insert(stss.end(), {'s', 't', 's', 's'});
    write8(stss, 0); // version
    write8(stss, 0); write8(stss, 0); write8(stss, 0); // flags
    write32(stss, frameCount); // entry_count
    for (uint32_t i = 1; i <= frameCount; i++) {
        write32(stss, i); // sample_number
    }
    
    // Build stsc (Sample-to-Chunk)
    auto stsc = BoxBuilder()
        .add32(0).add32(1)          // version/flags, entry_count
        .add32(1)                   // first_chunk
        .add32(frameCount)          // samples_per_chunk
        .add32(1)                   // sample_description_index
        .build("stsc");
    
    // Build stsz (Sample Size)
    std::vector<uint8_t> stsz;
    uint32_t stszSize = 20 + frameCount * 4; // header(20) + entries(frameCount*4)
    write32(stsz, stszSize);
    stsz.insert(stsz.end(), {'s', 't', 's', 'z'});
    write8(stsz, 0); // version
    write8(stsz, 0); write8(stsz, 0); write8(stsz, 0); // flags
    write32(stsz, 0); // sample_size = 0 (variable sizes)
    write32(stsz, frameCount); // sample_count
    for (uint32_t i = 0; i < frameCount; i++) {
        write32(stsz, 0); // placeholder
    }
    
    // Build stco (Chunk Offset)
    auto stco = BoxBuilder()
        .add32(0).add32(1)          // version/flags, entry_count
        .add32(0)                   // chunk_offset (placeholder)
        .build("stco");
    
    // Assemble stbl (Sample Table box)
    write32(stbl, 0); // size placeholder
    stbl.insert(stbl.end(), {'s', 't', 'b', 'l'});
    
    // All boxes now have sizes, insert as-is
    stbl.insert(stbl.end(), stsd.begin(), stsd.end());
    stbl.insert(stbl.end(), stts.begin(), stts.end());
    stbl.insert(stbl.end(), stss.begin(), stss.end());
    stbl.insert(stbl.end(), stsc.begin(), stsc.end());
    stbl.insert(stbl.end(), stsz.begin(), stsz.end());
    stbl.insert(stbl.end(), stco.begin(), stco.end());
    
    // Update stbl size
    uint32_t stblSize = stbl.size();
    stbl[0] = (stblSize >> 24) & 0xFF;
    stbl[1] = (stblSize >> 16) & 0xFF;
    stbl[2] = (stblSize >> 8) & 0xFF;
    stbl[3] = stblSize & 0xFF;
    
    return stbl;
}

std::vector<uint8_t> QuickTimeMuxer::createMdatBox(const std::vector<uint8_t>& frameData) {
    // Note: For snapshots, frameData should already contain SPS+PPS+Frame with length prefixes
    return BoxBuilder()
        .addBytes(frameData.data(), frameData.size())
        .build("mdat");
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

// getNALTypeName and getCurrentTimestamp removed - were only used for debug logging

// Flush write buffer to disk (like old MP4Muxer)
void QuickTimeMuxer::flushBufferToDisk(bool force) {
    if (m_writeBuffer.empty() || m_fd < 0) {
        return;
    }
    
    // Write buffered data to disk
    size_t totalWritten = 0;
    while (totalWritten < m_writeBuffer.size()) {
        ssize_t written = write(m_fd, m_writeBuffer.data() + totalWritten, 
                                m_writeBuffer.size() - totalWritten);
        if (written <= 0) {
            LOG(ERROR) << "[QuickTimeMuxer] Failed to flush buffer: " << written;
            break;
        }
        totalWritten += written;
    }
    
    if (totalWritten == m_writeBuffer.size()) {
        LOG(DEBUG) << "[QuickTimeMuxer] Flushed " << totalWritten << " bytes to disk" 
                   << (force ? " (forced)" : "");
    } else {
        LOG(ERROR) << "[QuickTimeMuxer] Partial flush: " << totalWritten << "/" << m_writeBuffer.size();
    }
    
    // Optionally force data to physical disk (only on forced flush or finalize)
    if (force && m_fd >= 0) {
        fsync(m_fd);
    }
    
    // Clear buffer and update flush time
    m_writeBuffer.clear();
    m_lastFlushTime = std::chrono::steady_clock::now();
}

// Check if buffer should be flushed (on keyframes at intervals)
bool QuickTimeMuxer::shouldFlushBuffer(bool isKeyFrame) {
    // Only flush on keyframes
    if (!isKeyFrame) {
        return false;
    }
    
    // Check time interval since last flush
    auto now = std::chrono::steady_clock::now();
    auto timeSinceFlush = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFlushTime);
    
    return timeSinceFlush.count() >= m_flushIntervalMs;
}