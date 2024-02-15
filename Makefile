CXX := clang++
CXXFLAGS := -std=c++20 -Wfatal-errors -Wall -Wextra -Wpedantic -Wconversion -Wshadow
LDFLAGS := -lnotcurses  -lnotcurses-core -lunistring -lm -ltinfo

yate: yate.o
	$(CXX) yate.o -o yate $(LDFLAGS)

yate.o : main.cpp text_buffer.h view.h
	$(CXX) -c $(CXXFLAGS) -o yate.o main.cpp

