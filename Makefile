CXX := clang++
CXXFLAGS := -std=c++20 -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow
CC := clang
CFLAGS := -std=c17 
LDFLAGS := -lnotcurses  -lnotcurses-core -lunistring -lm -ltinfo

yate: yate.o
	$(CXX) yate.o -o yate $(LDFLAGS) tree_sitter_langs/libtree-sitter.a

test: test.o $(TS_OBJS)
	$(CXX) -g  test.o -o test $(LDFLAGS) tree_sitter_langs/libtree-sitter.a

test.o: test.cpp text_buffer.h view.h util.h File.h Program.h
	$(CXX) -g -c $(CXXFLAGS) -o test.o test.cpp

yate.o : main.cpp text_buffer.h view.h util.h File.h Program.h
	$(CXX) -c $(CXXFLAGS) -o yate.o main.cpp

$(TS_OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@ $(CFLAGS) 

print: ; $(info $$var is [${TS_OBJS}])echo Hello world


debug: CXXFLAGS += -DDEBUG -g
debug: yate

.PHONY: print test
