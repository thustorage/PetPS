include ./Makefile_common
libmyvmem.so:vmem.cpp vmem.h
	$(CXX) $(DFLAGS) $(PFLAGS) -std=c++17 -I./ -O3 -fPIC -c $< -o $@ -lvmem
clean:
	rm -f *.o *.so