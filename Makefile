CXX := clang++
CXXFLAGS := -std=c++20 -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wno-vla-extension
CC := clang
CFLAGS := -std=c17 
LDFLAGS := -lnotcurses  -lnotcurses-core -lunistring -lm -ltinfo -ltree-sitter



yate: yate.o
	$(CXX) yate.o -o yate $(LDFLAGS)

debug: yate.o
	$(CXX) -g yate.o -o yate $(LDFLAGS)

test: test.o $(TS_OBJS)
	$(CXX) -g  test.o -o test $(LDFLAGS)

test.o: test.cpp text_buffer.h view.h util.h File.h Program.h
	$(CXX) -g -c $(CXXFLAGS) -o test.o test.cpp

yate.o : main.cpp text_buffer.h view.h util.h File.h Program.h
	$(CXX) -c $(CXXFLAGS) -o yate.o main.cpp

$(TS_OBJS): %.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@ $(CFLAGS) 

print: ; $(info $$var is [${TS_OBJS}])echo Hello world



.PHONY: print test debug
