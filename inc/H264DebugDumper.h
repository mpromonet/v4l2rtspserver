/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264DebugDumper.h
** 
** Debug utility for dumping H.264 stream data, V4L2 device info, and system info
** Enable by defining DEBUG_DUMP_H264_DATA in your code
** 
** -------------------------------------------------------------------------*/

#ifndef H264_DEBUG_DUMPER_H
#define H264_DEBUG_DUMPER_H

#include <string>
#include <fstream>
#include <iomanip>
#include <ctime>

// Forward declaration
class FullDataDumper {
private:
    std::string dumpDir;
    bool initialized;
    static FullDataDumper* instance;
    
    FullDataDumper();
    
public:
    static FullDataDumper* getInstance();
    
    void setDumpDir(const std::string& dir);
    
    // Dump V4L2 device information
    void dumpV4L2DeviceInfo(const std::string& devicePath);
    
    // Dump system information
    void dumpSystemInfo();
    
    // Dump H.264 stream data with full analysis
    void dumpH264StreamData(const std::string& sps, const std::string& pps, 
                           const unsigned char* h264Data, size_t h264Size,
                           int frameNumber, int width, int height);
    
    std::string getDumpDir() const { return dumpDir; }
};

#endif // H264_DEBUG_DUMPER_H

