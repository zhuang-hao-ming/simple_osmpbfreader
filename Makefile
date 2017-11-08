main: main.cc
	g++ main.cc -o main -lprotobuf-lite -losmpbf -lz -std=c++0x