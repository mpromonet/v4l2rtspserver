[![Build status](https://travis-ci.org/mpromonet/h264_v4l2_rtspserver.png)](https://travis-ci.org/mpromonet/h264_v4l2_rtspserver)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4644/badge.svg)](https://scan.coverity.com/projects/4644)
[![Coverage Status](https://coveralls.io/repos/github/mpromonet/h264_v4l2_rtspserver/badge.svg?branch=master)](https://coveralls.io/github/mpromonet/h264_v4l2_rtspserver?branch=master)


h264_v4l2_rtspserver
====================

It is an RTSP server feed from an Video4Linux device that support H264 format.

It is based on :
- libv4l-dev to capture video frames
- liblivemedia-dev (http://www.live555.com) to implement RTSP server

The RTSP server support :
- RTP/UDP unicast
- RTP/UDP multicast
- RTP/TCP
- RTP/RTSP/HTTP

License
------------
Domain public 

Dependencies
------------
 - liblivemedia-dev > live.2012.01.07 (need StreamReplicator)
 - libv4l-dev
 - liblog4cpp5-dev
 
Build
------- 
	cmake . && make

If it fails you will need to install libv4l-dev liblivemedia-dev liblog4cpp5-dev.  
If it still not work you will need to read Makefile.  

Install
--------- 
	make install

Build Package
-------------
	cpack .
	dpkg -i h264_v4l2_rtspserver*.deb

Using Raspberry Pi Camera
------------------------- 
This RTSP server works using Raspberry Pi camera with :
- the unofficial V4L2 driver for the Raspberry Pi Camera Module http://www.linux-projects.org/modules/sections/index.php?op=viewarticle&artid=14

	sudo uv4l --driver raspicam --auto-video_nr --encoding h264
- the official V4L2 driver bcm2835-v4l2

	sudo modprobe -v bcm2835-v4l2

Using with v4l2loopback
----------------------- 
For camera providing uncompress format [v4l2tools](https://github.com/mpromonet/v4l2tools) can compress the video to an intermediate virtual V4L2 device [v4l2loopback](https://github.com/umlaeute/v4l2loopback):

	/dev/video0 (camera device)-> v4l2compress_h264 -> /dev/video10 (v4l2loopback device) -> h264_v4l2_rtspserver

This workflow could be set using :

	modprobe v4l2loopback video_nr=10
	v4l2compress_h264 /dev/video0 /dev/video10 &
	h264_v4l2_rtspserver /dev/video10 &

Usage
-----
	./h264_v4l2_rtspserver [-v[v]] [-Q queueSize] [-O file] \
			       [-I interface] [-P RTSP port] [-T RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] \
			       [-r] [-s] [-W width] [-H height] [-F fps] [device1] [device2]
		 -v       : verbose
		 -vv      : very verbose
		 -Q length: Number of frame queue  (default 10)
		 -O output: Copy captured frame to a file or a V4L2 device
		 RTSP options :
		 -I addr  : RTSP interface (default autodetect)
		 -P port  : RTSP port (default 8554)
		 -T port  : RTSP over HTTP port (default 0)
		 -u url   : unicast url (default unicast)
		 -m url   : multicast url (default multicast)
		 -M addr  : multicast group:port (default is random_address:20000)
		 -c       : don't repeat config (default repeat config before IDR frame)
		 -t secs  : RTCP expiration timeout (default 65)
		 V4L2 options :
		 -r       : V4L2 capture using read interface (default use memory mapped buffers)
		 -s       : V4L2 capture using live555 mainloop (default use a separated reading thread)
		 -f       : V4L2 capture using current format (-W,-H,-F are ignored)
		 -W width : V4L2 capture width (default 640)
		 -H height: V4L2 capture height (default 480)
		 -F fps   : V4L2 capture framerate (default 25)
		 device   : V4L2 capture device (default /dev/video0)

