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
#include <iomanip>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <arpa/inet.h>

QuickTimeMuxer::QuickTimeMuxer() 
    : m_initialized(false), m_fd(-1), m_width(0), m_height(0), m_fps(30),
      m_mdatStartPos(0), m_currentPos(0),
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
    
    // Write mdat box header (media data will follow)
    // Note: We'll write moov AFTER mdat (at the end) as per live555 approach
    m_mdatStartPos = m_currentPos;
    uint32_t mdatSize = 0; // Will be updated later
    writeToFile(&mdatSize, 4);
    writeToFile("mdat", 4);
    
    return true;
}

bool QuickTimeMuxer::writeMoovBox() {
    // Calculate mdat size (from mdat start to current position)
    size_t mdatDataSize = m_currentPos - m_mdatStartPos - 8; // -8 for size+type
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
    
    // Seek to end of file to append moov
    if (lseek(m_fd, 0, SEEK_END) == -1) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to seek to end of file";
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
    
    // Write moov at the end of file
    written = write(m_fd, moovBox.data(), moovBox.size());
    if (written != static_cast<ssize_t>(moovBox.size())) {
        LOG(ERROR) << "[QuickTimeMuxer] Failed to write moov box: " << written << "/" << moovBox.size();
        return false;
    }
    
    // Sync file to disk
    fsync(m_fd);
    
    LOG(INFO) << "[QuickTimeMuxer] Wrote moov box (" << moovBox.size() << " bytes) at end, mdat size (" << mdatTotalSize << " bytes)";
    
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
    
    LOG(DEBUG) << "[Snapshot] ftyp size: " << ftypBox.size() << " bytes";
    LOG(DEBUG) << "[Snapshot] mp4Data size after ftyp: " << mp4Data.size() << " bytes";
    LOG(DEBUG) << "[Snapshot] mp4Data first 32 bytes after ftyp: 0x" << std::hex << std::setfill('0');
    for (size_t i = 0; i < 32 && i < mp4Data.size(); i++) {
        LOG(DEBUG) << std::setw(2) << (int)mp4Data[i];
    }
    LOG(DEBUG) << std::dec;
    
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
    
    LOG(DEBUG) << "[Snapshot] mdatContent size: " << mdatContent.size() << " bytes (SPS:" << sps.size() << " PPS:" << pps.size() << " Frame:" << dataSize << ")";
    
    // Create mdat box with all data
    auto mdatBox = createMdatBox(mdatContent);
    LOG(DEBUG) << "[Snapshot] mdatBox size: " << mdatBox.size() << " bytes, first 8 bytes: 0x" 
               << std::hex << std::setfill('0')
               << std::setw(2) << (int)mdatBox[0] << std::setw(2) << (int)mdatBox[1] 
               << std::setw(2) << (int)mdatBox[2] << std::setw(2) << (int)mdatBox[3] << " "
               << std::setw(2) << (int)mdatBox[4] << std::setw(2) << (int)mdatBox[5]
               << std::setw(2) << (int)mdatBox[6] << std::setw(2) << (int)mdatBox[7]
               << std::dec;
    
    uint32_t mdatOffset = ftypBox.size(); // Offset where mdat starts
    mp4Data.insert(mp4Data.end(), mdatBox.begin(), mdatBox.end());
    
    LOG(DEBUG) << "[Snapshot] mp4Data size after mdat: " << mp4Data.size() << " bytes";
    LOG(DEBUG) << "[Snapshot] mp4Data first 32 bytes after mdat: 0x" << std::hex << std::setfill('0');
    for (size_t i = 0; i < 32 && i < mp4Data.size(); i++) {
        LOG(DEBUG) << std::setw(2) << (int)mp4Data[i];
    }
    LOG(DEBUG) << std::dec;
    
    // Create moov box AFTER mdat (standard MP4 structure for streaming)
    // Note: stco offset in moov needs to point to SPS (first data in mdat)
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(sps.begin(), sps.end()),
        std::vector<uint8_t>(pps.begin(), pps.end()),
        width, height, fps, 1
    );
    
    // Fix stco offset in moov to point to actual mdat data position (after mdat header)
    // Find 'stco' box in moov and update the chunk offset
    uint32_t actualOffset = mdatOffset + 8; // Skip mdat header (8 bytes: size + 'mdat')
    
    // Search for 'stco' signature (0x7374636F) in moovBox
    LOG(DEBUG) << "[Snapshot] Searching for stco in moovBox (size=" << moovBox.size() << " bytes)";
    LOG(DEBUG) << "[Snapshot] moovBox first 32 bytes: 0x" << std::hex << std::setfill('0');
    for (size_t i = 0; i < 32 && i < moovBox.size(); i++) {
        LOG(DEBUG) << std::setw(2) << (int)moovBox[i];
    }
    LOG(DEBUG) << std::dec;
    
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
                LOG(DEBUG) << "[Snapshot] Fixed stco offset at position " << offsetPos 
                          << " (old: 0x" << std::hex << oldOffset << ", new: 0x" << actualOffset << ")" << std::dec;
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
                LOG(DEBUG) << "[Snapshot] Fixed stsz entry_sizes[0] at position " << entryPos 
                          << " (old: " << oldSize << ", new: " << frameSize << ")";
                break;
            }
        }
    }
    
    if (!stszFound) {
        LOG(WARN) << "[Snapshot] Could not find stsz box in moov to fix entry size!";
    }
    
    mp4Data.insert(mp4Data.end(), moovBox.begin(), moovBox.end());
    
    LOG(DEBUG) << "[Snapshot] Final MP4 size: " << mp4Data.size() << " bytes (ftyp:" << ftypBox.size() << " mdat:" << mdatBox.size() << " moov:" << moovBox.size() << ")";
    LOG(DEBUG) << "[Snapshot] First 48 bytes of MP4: 0x" << std::hex << std::setfill('0');
    for (size_t i = 0; i < 48 && i < mp4Data.size(); i++) {
        LOG(DEBUG) << std::setw(2) << (int)mp4Data[i];
    }
    LOG(DEBUG) << std::dec;
    
    return mp4Data;
}

std::vector<uint8_t> QuickTimeMuxer::createFtypBox() {
    std::vector<uint8_t> box;
    
    // Box size placeholder
    write32(box, 0);
    
    // Box type: 'ftyp'
    write32(box, 0x66747970);
    
    // Major brand: 'isom'
    write32(box, 0x69736F6D);
    
    // Minor version
    write32(box, 0x200);
    
    // Compatible brands
    write32(box, 0x69736F6D); // 'isom'
    write32(box, 0x69736F32); // 'iso2'
    write32(box, 0x61766331); // 'avc1'
    write32(box, 0x6D703431); // 'mp41'
    
    // Update size
    uint32_t size = box.size();
    box[0] = (size >> 24) & 0xFF;
    box[1] = (size >> 16) & 0xFF;
    box[2] = (size >> 8) & 0xFF;
    box[3] = size & 0xFF;
    
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
    // Based on live555 QuickTimeFileSink implementation
    // This creates a complete, valid MP4 moov box with all necessary atoms
    
    std::vector<uint8_t> moov;
    uint32_t timescale = (fps > 0) ? fps : 30; // H.264 timescale (frames per second)
    uint32_t duration = frameCount; // duration in timescale units
    
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
    uint32_t tkhdSize = 8 + tkhd.size();
    
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
    uint32_t mdhdSize = 8 + mdhd.size();
    
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
    uint32_t hdlrSize = 8 + hdlr.size();
    
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
    uint32_t vmhdSize = 8 + vmhd.size();
    
    // Build dinf/dref (Data Information)
    std::vector<uint8_t> dref;
    write32(dref, 0x64726566); // 'dref'
    write32(dref, 0); // version + flags
    write32(dref, 1); // entry_count
    // url entry
    write32(dref, 12); // size
    write32(dref, 0x75726C20); // 'url '
    write32(dref, 1); // version + flags (self-contained)
    uint32_t drefSize = 8 + dref.size();
    
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
    std::vector<uint8_t> stts;
    write32(stts, 24); // size
    stts.insert(stts.end(), {'s', 't', 't', 's'});
    write8(stts, 0); // version
    write8(stts, 0); write8(stts, 0); write8(stts, 0); // flags
    write32(stts, 1); // entry_count
    write32(stts, frameCount); // sample_count
    write32(stts, 1000); // sample_delta
    
    // Build stss (Sync Sample)
    std::vector<uint8_t> stss;
    write32(stss, 20); // size (for 1 frame)
    stss.insert(stss.end(), {'s', 't', 's', 's'});
    write8(stss, 0); // version
    write8(stss, 0); write8(stss, 0); write8(stss, 0); // flags
    write32(stss, frameCount); // entry_count
    for (uint32_t i = 1; i <= frameCount; i++) {
        write32(stss, i); // sample_number
    }
    
    // Build stsc (Sample-to-Chunk)
    std::vector<uint8_t> stsc;
    write32(stsc, 28); // size
    stsc.insert(stsc.end(), {'s', 't', 's', 'c'});
    write8(stsc, 0); // version
    write8(stsc, 0); write8(stsc, 0); write8(stsc, 0); // flags
    write32(stsc, 1); // entry_count
    write32(stsc, 1); // first_chunk
    write32(stsc, frameCount); // samples_per_chunk
    write32(stsc, 1); // sample_description_index
    
    // Build stsz (Sample Size)
    std::vector<uint8_t> stsz;
    write32(stsz, 24); // size (for 1 frame with 1 entry)
    stsz.insert(stsz.end(), {'s', 't', 's', 'z'});
    write8(stsz, 0); // version
    write8(stsz, 0); write8(stsz, 0); write8(stsz, 0); // flags
    write32(stsz, 0); // sample_size = 0 (variable sizes)
    write32(stsz, frameCount); // sample_count
    for (uint32_t i = 0; i < frameCount; i++) {
        write32(stsz, 0); // placeholder
    }
    
    // Build stco (Chunk Offset)
    std::vector<uint8_t> stco;
    write32(stco, 20); // size
    stco.insert(stco.end(), {'s', 't', 'c', 'o'});
    write8(stco, 0); // version
    write8(stco, 0); write8(stco, 0); write8(stco, 0); // flags
    write32(stco, 1); // entry_count
    write32(stco, 0); // chunk_offset (placeholder)
    
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
    std::vector<uint8_t> box;
    
    // Note: For snapshots, frameData should already contain SPS+PPS+Frame with length prefixes
    // Box size (8 + data size)
    write32(box, 8 + frameData.size());
    
    // Box type: 'mdat'
    write32(box, 0x6D646174);
    
    // Insert frame data as-is (should already have length prefixes)
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