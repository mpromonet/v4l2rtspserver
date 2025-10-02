/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264DebugDumper.cpp
** 
** Debug utility implementation for dumping H.264 stream data
** 
** -------------------------------------------------------------------------*/

#include "H264DebugDumper.h"
#include "logger.h"

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cstring>
#endif

#include <cstdlib>
#include <cstdio>

// Static instance
FullDataDumper* FullDataDumper::instance = nullptr;

FullDataDumper::FullDataDumper() : initialized(false) {
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

FullDataDumper* FullDataDumper::getInstance() {
    if (!instance) {
        instance = new FullDataDumper();
    }
    return instance;
}

void FullDataDumper::setDumpDir(const std::string& dir) {
    dumpDir = dir;
    std::string cmd = "mkdir -p " + dumpDir;
    int result = system(cmd.c_str());
    (void)result;
    LOG(NOTICE) << "[DUMP] Full data dumper directory set: " << dumpDir;
    initialized = true;
}

void FullDataDumper::dumpV4L2DeviceInfo(const std::string& devicePath) {
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

void FullDataDumper::dumpSystemInfo() {
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

void FullDataDumper::dumpH264StreamData(const std::string& sps, const std::string& pps, 
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

