TARGET=$(notdir $(realpath .))

LIVE555_CFLAGS=-I/usr/include/UsageEnvironment -I /usr/include/groupsock -I /usr/include/liveMedia -I /usr/include/BasicUsageEnvironment
LIVE555_FDFLAGS=-lliveMedia -lgroupsock  -lBasicUsageEnvironment -lUsageEnvironment 

all: /usr/include/libv4l2.h /usr/include/liveMedia/liveMedia.hh $(TARGET)

/usr/include/libv4l2.h:
	$(info Cannot find /usr/include/libv4l2.h)
	sudo apt-get install libv4l-dev

/usr/include/liveMedia/liveMedia.hh:
	$(info Cannot find /usr/include/liveMedia/liveMedia.hh)
	sudo apt-get install liblivemedia-dev

ODIR=obj

$(ODIR):
	mkdir -p $(ODIR)

$(ODIR)/%.o: %.cpp $(ODIR) $(DEPS) 
	g++ -g -c -o $@ $< $(LIVE555_CFLAGS) 

OBJ = $(patsubst %,$(ODIR)/%,$(foreach src,$(wildcard *.cpp),$(src:.cpp=.o)))
$(TARGET): $(OBJ)
	$(info $(OBJ))
	g++ -g -o $@ $^ $(LIVE555_CFLAGS) $(LIVE555_FDFLAGS) -lv4l2 


clean:
	rm -rf $(TARGET) $(ODIR)

