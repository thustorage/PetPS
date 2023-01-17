include ../common/Makefile_common
CFLAGS += -DINPLACE
all:CCEH.so

CCEH.so:CCEH_MSB.cpp CCEH.h 
	$(CXX) $(CFLAGS) -fPIC -shared $< -o $@ 
clean:
	rm -f *.o *.so lsb msb