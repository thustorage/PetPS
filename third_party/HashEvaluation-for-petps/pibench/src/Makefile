CC := g++
CFLAGS += -I../pcm -I../../hash/common -march=native  -fopenmp -std=c++17

TARGET := PiBench
SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %cpp,%o,$(SRCS))
all:$(TARGET)
%.o:%.cpp
	$(CC) $(CFLAGS) -c $<
$(TARGET):$(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -ldl ../pcm/libPCM.a
clean:
	rm -rf $(TARGET) *.o
