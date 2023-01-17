include ../common/Makefile_common
all:
	c++ -O3 $(PFLAGS) -I./ \
	-I/usr/local/include/valgrind -Itest/common \
	-Itest/. -std=gnu++11 -shared -fPIC -o clevel.so tests/clevel_hash/clevel_hash_cli.cpp -lpmemobj  -lpthread
clean:	
	rm *.a *.o *.so -f