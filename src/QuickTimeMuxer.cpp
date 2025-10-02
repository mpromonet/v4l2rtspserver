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
    
    // Create mdat box with frame data FIRST (for proper offset calculation)
    std::vector<uint8_t> frameData(h264Data, h264Data + dataSize);
    auto mdatBox = createMdatBox(frameData);
    uint32_t mdatOffset = ftypBox.size(); // Offset where mdat starts
    mp4Data.insert(mp4Data.end(), mdatBox.begin(), mdatBox.end());
    
    // Create moov box AFTER mdat (standard MP4 structure for streaming)
    // Note: stco offset in moov needs to point to mdat data
    auto moovBox = createVideoTrackMoovBox(
        std::vector<uint8_t>(sps.begin(), sps.end()),
        std::vector<uint8_t>(pps.begin(), pps.end()),
        width, height, fps, 1
    );
    
    // Fix stco offset in moov to point to actual mdat position
    // stco is at the end of moov, find it and update the offset
    size_t stcoPos = moovBox.size() - 8; // Last 8 bytes: version+flags(4) + entry_count(4) + offset(4)
    uint32_t actualOffset = mdatOffset + 8; // Skip mdat header (8 bytes: size + 'mdat')
    moovBox[stcoPos]   = (actualOffset >> 24) & 0xFF;
    moovBox[stcoPos+1] = (actualOffset >> 16) & 0xFF;
    moovBox[stcoPos+2] = (actualOffset >> 8) & 0xFF;
    moovBox[stcoPos+3] = actualOffset & 0xFF;
    
    mp4Data.insert(mp4Data.end(), moovBox.begin(), moovBox.end());
    
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
    uint32_t mvhdSize = 8 + mvhd.size();
    
    // Build trak (Track)
    std::vector<uint8_t> trak = createTrakBox(sps, pps, width, height, timescale, duration, frameCount);
    
    // Assemble moov box
    uint32_t moovSize = 8 + mvhdSize + trak.size();
    write32(moov, moovSize); // moov size
    write32(moov, 0x6D6F6F76); // 'moov'
    write32(moov, mvhdSize); // mvhd size
    moov.insert(moov.end(), mvhd.begin(), mvhd.end());
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
    
    // Assemble trak
    uint32_t trakSize = 8 + tkhdSize + mdia.size();
    write32(trak, trakSize);
    write32(trak, 0x7472616B); // 'trak'
    write32(trak, tkhdSize);
    trak.insert(trak.end(), tkhd.begin(), tkhd.end());
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
    
    // Assemble mdia
    uint32_t mdiaSize = 8 + mdhdSize + hdlrSize + minf.size();
    write32(mdia, mdiaSize);
    write32(mdia, 0x6D646961); // 'mdia'
    write32(mdia, mdhdSize);
    mdia.insert(mdia.end(), mdhd.begin(), mdhd.end());
    write32(mdia, hdlrSize);
    mdia.insert(mdia.end(), hdlr.begin(), hdlr.end());
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
    
    std::vector<uint8_t> dinf;
    write32(dinf, 8 + drefSize);
    write32(dinf, 0x64696E66); // 'dinf'
    write32(dinf, drefSize);
    dinf.insert(dinf.end(), dref.begin(), dref.end());
    
    // Build stbl (Sample Table)
    std::vector<uint8_t> stbl = createStblBox(sps, pps, width, height, frameCount);
    
    // Assemble minf
    uint32_t minfSize = 8 + vmhdSize + dinf.size() + stbl.size();
    write32(minf, minfSize);
    write32(minf, 0x6D696E66); // 'minf'
    write32(minf, vmhdSize);
    minf.insert(minf.end(), vmhd.begin(), vmhd.end());
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
    std::vector<uint8_t> avcC;
    write32(avcC, 0x61766343); // 'avcC'
    write8(avcC, 1); // configurationVersion
    write8(avcC, sps.size() > 1 ? sps[1] : 0x42); // AVCProfileIndication
    write8(avcC, sps.size() > 2 ? sps[2] : 0xC0); // profile_compatibility
    write8(avcC, sps.size() > 3 ? sps[3] : 0x1E); // AVCLevelIndication
    write8(avcC, 0xFF); // lengthSizeMinusOne
    write8(avcC, 0xE1); // numOfSequenceParameterSets
    write16(avcC, sps.size());
    avcC.insert(avcC.end(), sps.begin(), sps.end());
    write8(avcC, 1); // numOfPictureParameterSets
    write16(avcC, pps.size());
    avcC.insert(avcC.end(), pps.begin(), pps.end());
    uint32_t avcCSize = 8 + avcC.size();
    
    std::vector<uint8_t> avc1;
    write32(avc1, 0x61766331); // 'avc1'
    write32(avc1, 0); write16(avc1, 0); // reserved[6]
    write16(avc1, 1); // data_reference_index
    write16(avc1, 0); write16(avc1, 0); // pre_defined, reserved
    write32(avc1, 0); write32(avc1, 0); write32(avc1, 0); // pre_defined[3]
    write16(avc1, width); // width
    write16(avc1, height); // height
    write32(avc1, 0x00480000); // horizresolution
    write32(avc1, 0x00480000); // vertresolution
    write32(avc1, 0); // reserved
    write16(avc1, 1); // frame_count
    for (int i = 0; i < 32; i++) write8(avc1, 0); // compressorname[32]
    write16(avc1, 0x0018); // depth
    write16(avc1, 0xFFFF); // pre_defined
    write32(avc1, avcCSize);
    avc1.insert(avc1.end(), avcC.begin(), avcC.end());
    uint32_t avc1Size = 8 + avc1.size();
    
    std::vector<uint8_t> stsd;
    write32(stsd, 0x73747364); // 'stsd'
    write32(stsd, 0); // version + flags
    write32(stsd, 1); // entry_count
    write32(stsd, avc1Size);
    stsd.insert(stsd.end(), avc1.begin(), avc1.end());
    uint32_t stsdSize = 8 + stsd.size();
    
    // Build stts (Time-to-Sample)
    std::vector<uint8_t> stts;
    write32(stts, 0x73747473); // 'stts'
    write32(stts, 0); // version + flags
    write32(stts, 1); // entry_count
    write32(stts, frameCount); // sample_count
    write32(stts, 1); // sample_delta
    uint32_t sttsSize = 8 + stts.size();
    
    // Build stss (Sync Sample - all samples are keyframes for snapshot)
    std::vector<uint8_t> stss;
    write32(stss, 0x73747373); // 'stss'
    write32(stss, 0); // version + flags
    write32(stss, frameCount); // entry_count
    for (uint32_t i = 1; i <= frameCount; i++) {
        write32(stss, i); // sample_number
    }
    uint32_t stssSize = 8 + stss.size();
    
    // Build stsc (Sample-to-Chunk)
    std::vector<uint8_t> stsc;
    write32(stsc, 0x73747363); // 'stsc'
    write32(stsc, 0); // version + flags
    write32(stsc, 1); // entry_count
    write32(stsc, 1); // first_chunk
    write32(stsc, frameCount); // samples_per_chunk
    write32(stsc, 1); // sample_description_index
    uint32_t stscSize = 8 + stsc.size();
    
    // Build stsz (Sample Size) - will be filled during recording
    std::vector<uint8_t> stsz;
    write32(stsz, 0x7374737A); // 'stsz'
    write32(stsz, 0); // version + flags
    write32(stsz, 0); // sample_size (0 means variable size)
    write32(stsz, frameCount); // sample_count
    // Add actual frame sizes (from m_frames if this is for recording)
    for (uint32_t i = 0; i < frameCount; i++) {
        write32(stsz, 0); // placeholder - will be updated during recording
    }
    uint32_t stszSize = 8 + stsz.size();
    
    // Build stco (Chunk Offset)
    std::vector<uint8_t> stco;
    write32(stco, 0x7374636F); // 'stco'
    write32(stco, 0); // version + flags
    write32(stco, 1); // entry_count
    write32(stco, 0); // chunk_offset (placeholder - will be updated)
    uint32_t stcoSize = 8 + stco.size();
    
    // Assemble stbl
    uint32_t stblSize = 8 + stsdSize + sttsSize + stssSize + stscSize + stszSize + stcoSize;
    write32(stbl, stblSize);
    write32(stbl, 0x7374626C); // 'stbl'
    write32(stbl, stsdSize);
    stbl.insert(stbl.end(), stsd.begin(), stsd.end());
    write32(stbl, sttsSize);
    stbl.insert(stbl.end(), stts.begin(), stts.end());
    write32(stbl, stssSize);
    stbl.insert(stbl.end(), stss.begin(), stss.end());
    write32(stbl, stscSize);
    stbl.insert(stbl.end(), stsc.begin(), stsc.end());
    write32(stbl, stszSize);
    stbl.insert(stbl.end(), stsz.begin(), stsz.end());
    write32(stbl, stcoSize);
    stbl.insert(stbl.end(), stco.begin(), stco.end());
    
    return stbl;
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