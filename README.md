[![Build status](https://travis-ci.org/mpromonet/v4l2rtspserver.png)](https://travis-ci.org/mpromonet/v4l2rtspserver)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4644/badge.svg)](https://scan.coverity.com/projects/4644)
[![Coverage Status](https://coveralls.io/repos/github/mpromonet/v4l2rtspserver/badge.svg?branch=master)](https://coveralls.io/github/mpromonet/v4l2rtspserver?branch=master)

[![Release](https://img.shields.io/github/release/mpromonet/v4l2rtspserver.svg)](https://github.com/mpromonet/v4l2rtspserver/releases/latest)
[![Download](https://img.shields.io/github/downloads/mpromonet/v4l2rtspserver/total.svg)](https://github.com/mpromonet/v4l2rtspserver/releases/latest)


v4l2rtspserver
====================

This is an streamer feed from :
 - an Video4Linux device that support H264, JPEG or VP8 capture.
 - an ALSA device that support PCM 16 BE
 
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
 - libv4l2cpp [Unlicense](https://github.com/mpromonet/libv4l2cpp/blob/master/LICENSE)

Usage
-----
	./v4l2rtspserver [-v[v]] [-Q queueSize] [-O file] \
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
		 -U user:password : RTSP user and password
		 -R realm  : use md5 password 'md5(<username>:<realm>:<password>')
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
		 -f       : V4L2 capture using current capture format (-W,-H are ignored)
		 -fformat : V4L2 capture using format (-W,-H are used)
		 -W width : V4L2 capture width (default 640)
		 -H height: V4L2 capture height (default 480)
		 -F fps   : V4L2 capture framerate (default 25, 0 disable setting framerate)
		 
		 ALSA options :
		 -A freq    : ALSA capture frequency and channel (default 44100)
		 -C channels: ALSA capture channels (default 2)
		 -a fmt     : ALSA capture audio format (default S16_BE)
		 
		 device   : V4L2 capture device and/or ALSA device (default /dev/video0,default)

Authentification is enable when almost one user is defined. You can canfigure credentials :
 * using plain text password: -U foo:bar -U admin:admin
 * using md5 password: -R myrealm -U foo:$(echo -n foo:myrealm:bar | md5sum | cut -d- -f1) -U admin:$(echo -n admin:myrealm:admin | md5sum | cut -d- -f1)

It is possible to compose the RTSP session is different ways :
 * v4l2rtspserver /dev/video0              : one RTSP session with RTP video capturing V4L2 device /dev/video0
 * v4l2rtspserver ,default                 : one RTSP session with RTP audio capturing ALSA device default
 * v4l2rtspserver /dev/video0,default      : one RTSP session with RTP audio and RTP video
 * v4l2rtspserver /dev/video0 ,default     : two RTSP sessions first one with RTP video and second one with RTP audio
 * v4l2rtspserver /dev/video0 /dev/video1  : two RTSP sessions with an RTP video

Build
------- 
- Before build (optional)
	The build will try to install live555 package using apt-get, however in order to install live555 disabling check of port reuse, you can proceed like this:

		wget http://www.live555.com/liveMedia/public/live555-latest.tar.gz -O - | tar xvzf -
		cd live
		./genMakefiles linux
		sudo make CPPFLAGS=-DALLOW_RTSP_SERVER_PORT_REUSE=1 install

- Build  

		cmake . && make

	If it fails you will need to install liblivemedia-dev liblog4cpp5-dev.  
	If it still not work you will need to read Makefile.  

- Install (optional) 

		sudo make install

- Packaging  (optional)

		cpack .

Using Raspberry Pi Camera
------------------------- 
This RTSP server works using Raspberry Pi camera using :
- the unofficial V4L2 driver for the Raspberry Pi Camera Module http://www.linux-projects.org/uv4l/

	sudo uv4l --driver raspicam --auto-video_nr --encoding h264
- the official V4L2 driver bcm2835-v4l2

	sudo modprobe -v bcm2835-v4l2
	sudo echo bcm2835-v4l2 >> /etc/modules

Using with v4l2loopback
----------------------- 
For camera providing uncompress format [v4l2tools](https://github.com/mpromonet/v4l2tools) can compress the video to an intermediate virtual V4L2 device [v4l2loopback](https://github.com/umlaeute/v4l2loopback):

	/dev/video0 (camera device)-> v4l2compress_h264 -> /dev/video10 (v4l2loopback device) -> v4l2rtspserver

This workflow could be set using :

	modprobe v4l2loopback video_nr=10
	v4l2compress_h264 /dev/video0 /dev/video10 &
	v4l2rtspserver /dev/video10 &


Receiving HTTP streams
-----------------------
When v4l2rtspserver is started with '-S' arguments it give access to streams through HTTP. These streams could be reveced :

	* for MPEG-DASH with :   
           MP4Client http://..../unicast.mpd   
	* for HLS with :  
           vlc http://..../unicast.m3u8  
           gstreamer-launch-1.0 playbin uri=http://.../unicast.m3u8  

