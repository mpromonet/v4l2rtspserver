h264_v4l2_rtspserver
====================

This provide an RTSP server feed from an Video4Linux device that support H264 format.

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

License
-------
Domain public 

Build
-----
make

Usage
-----
	./h264_v4l2_rtspserver [-v][-m][-P RTSP port][-P RTSP/HTTP port][-Q queueSize] [-W width] [-H height] [-F fps] [-O file] [device]
		 -v       : Verbose 
		 -Q length: Number of frame queue  (default 10)
		 -O file  : Dump capture to a file
		 -m       : Enable multicast output
		 -P port  : RTSP port (default 8554)
		 -H port  : RTSP over HTTP port (default 8080)
		 -F fps   : V4L2 capture framerate (default 25)
		 -W width : V4L2 capture width (default 640)
		 -H height: V4L2 capture height (default 480)
		 device   : V4L2 capture device (default /dev/video0)
