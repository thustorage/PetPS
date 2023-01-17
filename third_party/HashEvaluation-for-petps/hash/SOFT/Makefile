include ../common/Makefile_common
all: libssmem.so SOFT.so

libssmem.so: ./ssmem.cpp ./ssmem.h
	$(CXX) $(CFLAGS) -Wall -I./ -fPIC -c -o $@ $< 
SOFT.so:SOFT.cpp PNode.h rand_r_32.h SOFT.h SOFTList.h utilities.h VolatileNode.h libssmem.so
	$(CXX) $(CFLAGS) -I./ -L./ -pthread  -fPIC -shared -o $@ $< -lssmem 
clean:
	rm -f *.o *.so SOFT
