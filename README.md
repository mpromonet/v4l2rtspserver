[![Build status](https://travis-ci.org/mpromonet/v4l2rtspserver.png)](https://travis-ci.org/mpromonet/v4l2rtspserver)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4644/badge.svg)](https://scan.coverity.com/projects/4644)
[![Coverage Status](https://coveralls.io/repos/github/mpromonet/v4l2rtspserver/badge.svg?branch=master)](https://coveralls.io/github/mpromonet/v4l2rtspserver?branch=master)


v4l2rtspserver
====================

This is an streamer feed from an Video4Linux device that support H264/JPEG or VP8 capture.

The RTSP server support :
- RTP/UDP unicast
- RTP/UDP multicast
- RTP/TCP
- RTP/RTSP/HTTP

The HTTP server support :
- HLS
- MPEG-DASH

License
------------
Domain public 

Dependencies
------------
 - liblivemedia-dev [License LGPL](http://www.live555.com/liveMedia/) > live.2012.01.07 (need StreamReplicator)
 - liblog4cpp5-dev  [License LGPL](http://log4cpp.sourceforge.net/#license)

Download
--------
[Latest build](https://github.com/mpromonet/h264_v4l2_rtspserver/releases/latest/) 
 
Build
------- 
	cmake . && make

If it fails you will need to install liblivemedia-dev liblog4cpp5-dev.  
If it still not work you will need to read Makefile.  

In order to build live555 disabling check of port reuse, you can proceed like this:

	wget http://www.live555.com/liveMedia/public/live555-latest.tar.gz -O - | tar xvzf -
	cd live
	./genMakefile linux
	sudo make CPPFLAGS=-DALLOW_RTSP_SERVER_PORT_REUSE=1 install

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
			       [-I interface] [-P RTSP port] [-p RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] \
			       [-r] [-s] [-W width] [-H height] [-F fps] [device1] [device2]
		 -v       : verbose
		 -vv      : very verbose
		 -Q length: Number of frame queue  (default 10)
		 -O output: Copy captured frame to a file or a V4L2 device
		 RTSP options :
		 -I addr  : RTSP interface (default autodetect)
		 -P port  : RTSP port (default 8554)
		 -p port  : RTSP over HTTP port (default 0)
		 -u url   : unicast url (default unicast)
		 -m url   : multicast url (default multicast)
		 -M addr  : multicast group:port (default is random_address:20000)
		 -c       : don't repeat config (default repeat config before IDR frame)
		 -t secs  : RTCP expiration timeout (default 65)
		 -T       : send Transport Stream instead of elementary Stream
		 -S secs  : HTTP segment duration (enable HLS & MPEG-DASH)
		 V4L2 options :
		 -r       : V4L2 capture using read interface (default use memory mapped buffers)
		 -w       : V4L2 capture using write interface (default use memory mapped buffers)
		 -s       : V4L2 capture using live555 mainloop (default use a separated reading thread)
		 -f       : V4L2 capture using current capture format (-W,-H,-F are ignored)
		 -fformat : V4L2 capture using format (-W,-H,-F are used)
		 -W width : V4L2 capture width (default 640)
		 -H height: V4L2 capture height (default 480)
		 -F fps   : V4L2 capture framerate (default 25)
		 device   : V4L2 capture device (default /dev/video0)

