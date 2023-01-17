include ../common/Makefile_common
all: PCLHT.so
PCLHT.so:clht_lb_res.cpp clht_lb_res.h
	$(CXX) $(CFLAGS) -fPIC -shared -o $@ $<
clean:
	rm -f *.o *.so