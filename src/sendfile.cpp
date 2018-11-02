#include <iostream>
#include <thread>
#include <mutex>

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include "util.h"

#define TIMEOUT 10

using namespace std;

int socketObj;
struct sockaddr_in serverAddress, clientAddress;

int windowLength;
bool *isWindowAcked;
time_stamp *windowSentTime;
int lar, lfs;

time_stamp TMIN = current_time();
mutex windowInfoMutex;

void listenAck() {
    char ack[ACK_SIZE];
    int ackSize;
    int ackSequenceNum;
    bool ackError;
    bool ackNeg;

    while (true) {
        socklen_t serverAddressSize;
        ackSize = recvfrom(socketObj, (char *)ack, ACK_SIZE, 
                MSG_WAITALL, (struct sockaddr *) &serverAddress, 
                &serverAddressSize);
        ackError = read_ack(&ackSequenceNum, &ackNeg, ack);

        windowInfoMutex.lock();

        if (!ackError && ackSequenceNum > lar && ackSequenceNum <= lfs) {
            if (!ackNeg) {
                isWindowAcked[ackSequenceNum - (lar + 1)] = true;
            } else {
                windowSentTime[ackSequenceNum - (lar + 1)] = TMIN;
            }
        }

        windowInfoMutex.unlock();
    }
}

int main(int argc, char *argv[]) {
    char *destinationIP;
    int destinationPort;
    int maxBufferSize;
    struct hostent *destinationHnet;
    char *fname;

    if (argc == 6) {
        fname = argv[1];
        windowLength = atoi(argv[2]);
        maxBufferSize = MAX_DATA_SIZE * (int) atoi(argv[3]);
        destinationIP = argv[4];
        destinationPort = atoi(argv[5]);
    } else {
        cerr << "usage: ./sendfile <filename> <window_len> <buffer_size> <destination_ip> <destination_port>" << endl;
        return 1; 
    }

    destinationHnet = gethostbyname(destinationIP); 
    if (!destinationHnet) {
        cerr << "unknown host: " << destinationIP << endl;
        return 1;
    }

    memset(&serverAddress, 0, sizeof(serverAddress)); 
    memset(&clientAddress, 0, sizeof(clientAddress)); 

    serverAddress.sin_family = AF_INET;
    bcopy(destinationHnet->h_addr, (char *)&serverAddress.sin_addr, 
            destinationHnet->h_length); 
    serverAddress.sin_port = htons(destinationPort);

    clientAddress.sin_family = AF_INET;
    clientAddress.sin_addr.s_addr = INADDR_ANY; 
    clientAddress.sin_port = htons(0);

    if ((socketObj = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cerr << "socket creation failed" << endl;
        return 1;
    }

    if (::bind(socketObj, (const struct sockaddr *)&clientAddress, 
            sizeof(clientAddress)) < 0) { 
        cerr << "socket binding failed" << endl;
        return 1;
    }

    if (access(fname, F_OK) == -1) {
        cerr << "file doesn't exist: " << fname << endl;
        return 1;
    }

    FILE *file = fopen(fname, "rb");
    char buffer[maxBufferSize];
    int bufferSize;
    thread recv_thread(listenAck);

    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    int frameSize;
    int dataSize;

    bool readDone = false;
    int bufferNum = 0;
    while (!readDone) {

        bufferSize = fread(buffer, 1, maxBufferSize, file);
        if (bufferSize == maxBufferSize) {
            char temp[1];
            int nextBufferSize = fread(temp, 1, 1, file);
            if (nextBufferSize == 0) readDone = true;
            int error = fseek(file, -1, SEEK_CUR);
        } else if (bufferSize < maxBufferSize) {
            readDone = true;
        }
        
        windowInfoMutex.lock();

        int sequenceCount = bufferSize / MAX_DATA_SIZE + ((bufferSize % MAX_DATA_SIZE == 0) ? 0 : 1);
        int sequenceNumber;
        windowSentTime = new time_stamp[windowLength];
        isWindowAcked = new bool[windowLength];
        bool isWindowSent[windowLength];
        for (int i = 0; i < windowLength; i++) {
            isWindowAcked[i] = false;
            isWindowSent[i] = false;
        }
        lar = -1;
        lfs = lar + windowLength;

        windowInfoMutex.unlock();

        bool sent = false;
        while (!sent) {

            windowInfoMutex.lock();

            if (isWindowAcked[0]) {
                int shift = 1;
                for (int i = 1; i < windowLength; i++) {
                    if (!isWindowAcked[i]) break;
                    shift += 1;
                }
                for (int i = 0; i < windowLength - shift; i++) {
                    isWindowSent[i] = isWindowSent[i + shift];
                    isWindowAcked[i] = isWindowAcked[i + shift];
                    windowSentTime[i] = windowSentTime[i + shift];
                }
                for (int i = windowLength - shift; i < windowLength; i++) {
                    isWindowSent[i] = false;
                    isWindowAcked[i] = false;
                }
                lar += shift;
                lfs = lar + windowLength;
            }

            windowInfoMutex.unlock();

            for (int i = 0; i < windowLength; i ++) {
                sequenceNumber = lar + i + 1;

                if (sequenceNumber < sequenceCount) {
                    windowInfoMutex.lock();

                    if (!isWindowSent[i] || (!isWindowAcked[i] && (elapsed_time(current_time(), windowSentTime[i]) > TIMEOUT))) {
                        int bufferShift = sequenceNumber * MAX_DATA_SIZE;
                        dataSize = (bufferSize - bufferShift < MAX_DATA_SIZE) ? (bufferSize - bufferShift) : MAX_DATA_SIZE;
                        memcpy(data, buffer + bufferShift, dataSize);
                        
                        bool eot = (sequenceNumber == sequenceCount - 1) && (readDone);
                        frameSize = create_frame(sequenceNumber, frame, data, dataSize, eot);

                        sendto(socketObj, frame, frameSize, 0, 
                                (const struct sockaddr *) &serverAddress, sizeof(serverAddress));
                        isWindowSent[i] = true;
                        windowSentTime[i] = current_time();
                    }

                    windowInfoMutex.unlock();
                }
            }

            if (lar >= sequenceCount - 1) sent = true;
        }

        cout << "\r" << "Sent " << (unsigned long long) bufferNum * (unsigned long long) 
                maxBufferSize + (unsigned long long) bufferSize << " bytes" << flush;
        bufferNum += 1;
        if (readDone) break;
    }
    
    fclose(file);
    delete [] isWindowAcked;
    delete [] windowSentTime;
    recv_thread.detach();

    cout << "\nAll done :)" << endl;
    return 0;
}
