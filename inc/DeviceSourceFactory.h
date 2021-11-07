/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** DeviceSourceFactory.h
** 
** V4L2 live555 source 
**
** -------------------------------------------------------------------------*/

#pragma once

#include <linux/videodev2.h>

#include "V4L2DeviceSource.h"
#include "H264_V4l2DeviceSource.h"

class DeviceSourceFactory {
    public:
		static FramedSource* createFramedSource(UsageEnvironment* env, int format, DeviceInterface* devCapture, int queueSize = 5, bool useThread = true, int outfd = -1, bool repeatConfig = true) {
            FramedSource* source = NULL;
            if (format == V4L2_PIX_FMT_H264)
            {
                source = H264_V4L2DeviceSource::createNew(*env, devCapture, outfd, queueSize, useThread, repeatConfig, false);
            }
            else if (format == V4L2_PIX_FMT_HEVC)
            {
                source = H265_V4L2DeviceSource::createNew(*env, devCapture, outfd, queueSize, useThread, repeatConfig, false);
            }
            else 
            {
                source = V4L2DeviceSource::createNew(*env, devCapture, outfd, queueSize, useThread);
            }
            return source;
        }

        static StreamReplicator* createStreamReplicator(UsageEnvironment* env, int format, DeviceInterface* devCapture, int queueSize = 5, bool useThread = true, int outfd = -1, bool repeatConfig = true) {
            StreamReplicator* replicator = NULL;
            FramedSource* framedSource = DeviceSourceFactory::createFramedSource(env, format, devCapture, queueSize, useThread, outfd, repeatConfig);
            if (framedSource != NULL) 
            {						
                // extend buffer size if needed
                if (devCapture->getBufferSize() > OutPacketBuffer::maxSize)
                {
                    OutPacketBuffer::maxSize = devCapture->getBufferSize();
                }						
                replicator = StreamReplicator::createNew(*env, framedSource, false);
            }
            return replicator;
        }
}; 
