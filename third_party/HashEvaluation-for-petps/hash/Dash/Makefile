include ../common/Makefile_common
all:pmdk Dash.so
CFLAGS += $(DA_FLAGS) -I../common -DPMEM -Ipmdk/src/include -I./_deps/epoch_reclaimer-src -Wno-long-long  -march=native -Ofast -O3 -DNDEBUG -std=gnu++17
FL = -Lpmdk/src/nondebug -Wl,-rpath,/home/aim/hdk/PMHash/hash/Dash/pmdk/src/nondebug -lpthread -lgflags -lpmemobj -lpmem 

pmdk:
	make -C ./pmdk/src

Dash.so:src/ex_finger.cpp src/ex_finger.h src/Hash.h
	$(CXX) $(CFLAGS) -fPIC -shared $<  -o $@ $(FL)

clean:
	rm -f example *.o *.so
	make -C ./pmdk/src clean
.PHONY:pmdk