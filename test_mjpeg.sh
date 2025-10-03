#!/bin/bash

# Test MJPEG snapshot functionality
echo "=== Testing MJPEG Snapshot ==="

# Build command
BUILD_CMD="cd /Users/350d/Library\ Mobile\ Documents/com~apple~CloudDocs/GIT/v4l2rtspserver/build && cmake .. && make"

# Test command for MJPEG
TEST_CMD="./v4l2rtspserver -fMJPEG -W 640 -H 480 -F 30 -j /tmp/test_mjpeg.jpg /dev/video0 -vv"

# HTTP snapshot test
SNAPSHOT_TEST="curl -o /tmp/snapshot_mjpeg.jpg http://localhost:8554/snapshot"

echo ""
echo "Commands to run:"
echo ""
echo "1. Build:"
echo "   cd build && cmake .. && make"
echo ""
echo "2. Run server with MJPEG:"
echo "   $TEST_CMD"
echo ""
echo "3. In another terminal, test HTTP snapshot:"
echo "   $SNAPSHOT_TEST"
echo ""
echo "4. Check files:"
echo "   file /tmp/test_mjpeg.jpg /tmp/snapshot_mjpeg.jpg"
echo "   open /tmp/test_mjpeg.jpg"
echo "   open /tmp/snapshot_mjpeg.jpg"
echo ""
echo "Expected results:"
echo "   - /tmp/test_mjpeg.jpg should be a valid JPEG (saved from -j parameter)"
echo "   - /tmp/snapshot_mjpeg.jpg should be a valid JPEG (from HTTP /snapshot endpoint)"
echo "   - Both should display correctly"
echo ""
