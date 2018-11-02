all: recvfile sendfile

recvfile: src/util.h src/util.cpp src/recvfile.cpp
	g++ -std=c++11 -pthread src/util.cpp src/recvfile.cpp -o recvfile

sendfile: src/util.h src/util.cpp src/sendfile.cpp
	g++ -std=c++11 -pthread src/util.cpp src/sendfile.cpp -o sendfile

clean: recvfile sendfile
	rm -f recvfile sendfile
