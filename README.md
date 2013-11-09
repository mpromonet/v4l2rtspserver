h264_v4l2_rtspserver
====================

This implement an RTSP server feed reading H264 from a Video4Linux device.

It involves :
- libv4l to capture video frames
- live555 (http://www.live555.com) to implement RTSP server

The RTSP server support :
- RTP/UDP unicast
- RTP/UDP multicast
- RTP/TCP
- RTP/RTSP/HTTP

For the raspberry a V4L2 driver for the Raspberry Pi Camera Module in available 
http://www.linux-projects.org/modules/sections/index.php?op=viewarticle&artid=14

