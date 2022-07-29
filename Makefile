
GCC = gcc

# 选择不同的链接顺序,将调用不同的函数
CFLAGS += -l1 -l2
#CFLAGS += -l2 -l1

all: library1 library2 test

library1:
	@$(GCC) -fPIC -shared -o ./lib1.so ./lib1/lib1.c -I./lib1

library2:
	@$(GCC) -fPIC -shared -o ./lib2.so ./lib2/lib2.c -I./lib2

test:
	@$(GCC) -o ./out ./main/main.c ./main/symbolmatch.c \
	-I./lib1 -I./lib2 -I./main -L./ \
	-Wall $(CFLAGS)

clean:
	@rm -rf out *.so
