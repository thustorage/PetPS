DIRS = $(shell find ./hash -mindepth 1 -maxdepth 1 -type d)
all: bin
pcm:
	@make -C pibench/pcm ;
pibench:pcm
	@make -C pibench/src 
hash:pibench
	@make -C ./hash/common ;\
	for dir in $(DIRS);\
	do\
		make -C $$dir;\
	done
bin:hash
	@mkdir bin;find ./ -name "*.so" ! -name "lib*" -type f -exec cp {} ./bin/ \;;\
	cp pibench/src/PiBench ./bin;
	

clean:
	@make -C pibench/src clean;\
	rm -rf bin;\
	for dir in $(DIRS);\
	do\
		make clean -C $$dir;\
	done
cleanhash:
	@rm -rf bin;\
	for dir in $(DIRS);\
	do\
		make clean -C $$dir;\
	done
.PHONY: hash