.PHONY: clean test

CXXFLAGS=-std=c++11 -g -I../src -I../inc

all: test_instructions

test: test_instructions
	./test_instructions

test_instructions: test_instructions.cc ../src/instruction.cc

clean:
	rm -rf test_instructions
