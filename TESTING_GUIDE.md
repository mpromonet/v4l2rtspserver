# v4l2rtspserver Testing Guide

## Overview

This guide explains how to test snapshot and recording functionality with different video formats.

---

## Supported Formats

### ✅ H.264 (Recommended for Raspberry Pi Camera)
- **Snapshots**: Single-frame MP4 files
- **Recording**: Multi-frame MP4 files
- **HTTP endpoint**: `/snapshot` returns MP4
- **Works with**: Hardware H.264 encoders (Pi Camera, USB H.264 cameras)

### ✅ MJPEG
- **Snapshots**: JPEG images
- **Recording**: Raw MJPEG stream (`.mjpeg` file, NOT `.mp4`!)
- **HTTP endpoint**: `/snapshot` returns JPEG
- **Works with**: USB webcams with MJPEG support

### ❌ MJPEG → MP4 Recording (NOT SUPPORTED)
**MJPEG cannot be stored in MP4 container!** MP4 requires H.264/H.265 codec.

If you try `-fMJPEG -O output.mp4`, you'll get an error:
```
[ERROR] MJPEG format cannot be recorded to MP4 container!
[ERROR] MP4 requires H.264/H.265 codec, but device is outputting MJPEG.
[ERROR] Solutions:
[ERROR]   1. Use -fH264 for hardware H.264 encoding (recommended for Raspberry Pi Camera)
[ERROR]   2. Remove -O parameter to disable recording  
[ERROR]   3. Change output file extension to .mjpeg: -O output.mjpeg
```

---

## Testing Commands

### Test 1: H.264 Snapshots + Recording (Raspberry Pi Camera)

```bash
# Start server with H.264
./v4l2rtspserver -fH264 -W 640 -H 360 -F 30 \
  -j /tmp/snapshot.mp4 \
  -O /tmp/recording.mp4 \
  /dev/video0 -vv

# In another terminal:
# 1. Test HTTP snapshot (works immediately, no RTSP client needed!)
curl -o /tmp/http_snapshot.mp4 http://localhost:8554/snapshot

# 2. Wait 5-10 seconds, then stop server (Ctrl+C)

# 3. Verify files
file /tmp/snapshot.mp4 /tmp/recording.mp4 /tmp/http_snapshot.mp4
ffprobe /tmp/snapshot.mp4
ffprobe /tmp/recording.mp4
mp4box -info /tmp/recording.mp4

# 4. Play files
vlc /tmp/snapshot.mp4      # Single frame
vlc /tmp/recording.mp4     # Full recording
vlc /tmp/http_snapshot.mp4 # HTTP snapshot
```

**Expected:**
- ✅ All 3 MP4 files are valid
- ✅ `ffprobe` shows H.264 codec
- ✅ Files play in VLC/QuickTime
- ✅ `/snapshot` endpoint works WITHOUT needing RTSP connection

---

### Test 2: MJPEG Snapshots (USB Webcam)

```bash
# Start server with MJPEG (NO -O parameter!)
./v4l2rtspserver -fMJPEG -W 640 -H 480 -F 30 \
  -j /tmp/snapshot.jpg \
  /dev/video0 -vv

# In another terminal:
# Test HTTP snapshot (works immediately!)
curl -o /tmp/http_snapshot.jpg http://localhost:8554/snapshot

# Verify files
file /tmp/snapshot.jpg /tmp/http_snapshot.jpg
open /tmp/snapshot.jpg
open /tmp/http_snapshot.jpg
```

**Expected:**
- ✅ Both JPEG files are valid images
- ✅ Files open in image viewers
- ✅ `/snapshot` endpoint works WITHOUT needing RTSP connection

---

### Test 3: MJPEG Raw Stream Recording (Advanced)

If you need to record MJPEG stream:

```bash
# Use .mjpeg extension (NOT .mp4!)
./v4l2rtspserver -fMJPEG -W 640 -H 480 -F 30 \
  -j /tmp/snapshot.jpg \
  -O /tmp/recording.mjpeg \
  /dev/video0 -vv

# Stop after 5 seconds

# Play raw MJPEG stream
ffplay /tmp/recording.mjpeg
# Or convert to video
ffmpeg -i /tmp/recording.mjpeg -c:v libx264 /tmp/output.mp4
```

---

## Architecture Notes

### Snapshot Independence from RTSP

**Old behavior (BROKEN):**
- Snapshots required RTSP client connection
- `MJPEGVideoSource` only created when client connects
- `/snapshot` endpoint returned "No snapshot available"

**New behavior (FIXED):**
- Snapshots work immediately after server start
- MJPEG frames captured directly from `V4L2DeviceSource`
- No RTSP client needed for snapshots
- `/snapshot` endpoint always available

### Format Compatibility

| Format | Snapshot | Recording | HTTP `/snapshot` |
|--------|----------|-----------|------------------|
| **H.264** | ✅ MP4 | ✅ MP4 | ✅ MP4 |
| **MJPEG** | ✅ JPEG | ✅ `.mjpeg` only | ✅ JPEG |
| **MJPEG→MP4** | ❌ | ❌ NOT SUPPORTED | ❌ |

---

## Troubleshooting

### "Cannot set pixelformat to:MJPE"
**Problem**: Camera doesn't support MJPEG.  
**Solution**: Use `-fH264` instead (Raspberry Pi Camera supports H.264 natively).

### "test_O.mp4 doesn't play"
**Problem**: You used `-fMJPEG -O test.mp4` (not supported).  
**Solution**: Either:
1. Use `-fH264` for MP4 recording
2. Remove `-O` parameter
3. Use `-O test.mjpeg` for raw MJPEG stream

### "/snapshot returns 'No snapshot available'"
**Problem**: Old version before architecture fix.  
**Solution**: Update to latest version (commit 20d5355 or later).

---

## Implementation Details

See `IMPLEMENTATION_NOTES.md` for full technical details about the QuickTimeMuxer implementation and live555 compatibility.

---

**Status**: ✅ Production-ready  
**Tested on**: Raspberry Pi Camera Module v2, USB Webcams  
**Last updated**: 2025-01-03
