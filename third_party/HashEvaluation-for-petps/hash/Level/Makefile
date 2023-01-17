include ../common/Makefile_common
all: Level.so

Level.so:Level_hashing.cpp Level_hashing.h
	$(CXX) $(CFLAGS) -fPIC -shared $< -o $@  

clean:
	rm -rf *.o *.so level 
