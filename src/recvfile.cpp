#include <iostream>
#include <thread>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include "util.h"

#define STDBY_TIME 5000
using namespace std;

int socketObj;
struct sockaddr_in serverAddr, clientAddress;

void sendAck() {
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    int frameSize;
    int dataSize;
    socklen_t clientAddressSize;
    
    int recvSequenceNumber;
    bool errorStatus;
    bool eot;

    while (true) {
        frameSize = recvfrom(socketObj, (char *)frame, MAX_FRAME_SIZE, MSG_WAITALL, (struct sockaddr *) &clientAddress, &clientAddressSize);
        errorStatus = read_frame(&recvSequenceNumber, data, &dataSize, &eot, frame);
        create_ack(recvSequenceNumber, ack, errorStatus);
        sendto(socketObj, ack, ACK_SIZE, 0, (const struct sockaddr *) &clientAddress, clientAddressSize);
    }
}

int main(int argc, char * argv[]) {
    int port;
    int lengthOfWindow;
    int maxBufferSize;
    char *fname;

    if (argc == 5) {
        fname = argv[1];
        lengthOfWindow = (int) atoi(argv[2]);
        maxBufferSize = MAX_DATA_SIZE * (int) atoi(argv[3]);
        port = atoi(argv[4]);
    } else {
        cout << "wrong argument" << endl;
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr)); 
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; 
    serverAddr.sin_port = htons(port);

    memset(&clientAddress, 0, sizeof(clientAddress)); 

    if ((socketObj = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        count << "socket failed" << endl;
        return 1;
    }

    if (::bind(socketObj, (const struct sockaddr *)&serverAddr, 
            sizeof(serverAddr)) < 0) { 
        cout << "socket binding error" << endl;
        return 1;
    }

    FILE *file = fopen(fname, "wb");
    char buffer[maxBufferSize];
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    int bufferSize;
    int frameSize;
    int dataSize;
    int lfr, laf;
    int recvSequenceNumber;
    int bufferNum = 0;
    bool eot;
    bool frameError;
    bool recv = true;

    while (recv) {
        bufferSize = maxBufferSize;
        memset(buffer, 0, bufferSize);
    
        int recvSequenceCount = (int) maxBufferSize / MAX_DATA_SIZE;
        bool isWindowRecvMasked[lengthOfWindow];
        for (int i = 0; i < lengthOfWindow; i++) {
            isWindowRecvMasked[i] = false;
        }
        lfr = -1;
        laf = lfr + lengthOfWindow;
        
        while (true) {
            socklen_t clientAddressSize;
            frameSize = recvfrom(socketObj, (char *) frame, MAX_FRAME_SIZE, 
                    MSG_WAITALL, (struct sockaddr *) &clientAddress, 
                    &clientAddressSize);
            frameError = read_frame(&recvSequenceNumber, data, &dataSize, &eot, frame);

            create_ack(recvSequenceNumber, ack, frameError);
            sendto(socketObj, ack, ACK_SIZE, 0, 
                    (const struct sockaddr *) &clientAddress, clientAddressSize);

            if (recvSequenceNumber <= laf) {
                if (!frameError) {
                    cout << "Frame Received [" << recvSequenceNumber << "] : " << data << "\n";
                    int bufferShift = recvSequenceNumber * MAX_DATA_SIZE;

                    if (recvSequenceNumber == lfr + 1) {
                        memcpy(buffer + bufferShift, data, dataSize);

                        int shift = 1;
                        for (int i = 1; i < lengthOfWindow; i++) {
                            if (!isWindowRecvMasked[i]) break;
                            shift += 1;
                        }
                        for (int i = 0; i < lengthOfWindow - shift; i++) {
                            isWindowRecvMasked[i] = isWindowRecvMasked[i + shift];
                        }
                        for (int i = lengthOfWindow - shift; i < lengthOfWindow; i++) {
                            isWindowRecvMasked[i] = false;
                        }
                        lfr += shift;
                        laf = lfr + lengthOfWindow;
                    } else if (recvSequenceNumber > lfr + 1) {
                        if (!isWindowRecvMasked[recvSequenceNumber - (lfr + 1)]) {
                            memcpy(buffer + bufferShift, data, dataSize);
                            isWindowRecvMasked[recvSequenceNumber - (lfr + 1)] = true;
                        }
                    }

                    if (eot) {
                        bufferSize = bufferShift + dataSize;
                        recvSequenceCount = recvSequenceNumber + 1;
                        recv = false;
                    }
                }
            }
            
            if (lfr >= recvSequenceCount - 1) break;
        }

        cout << "\r" << "Receive " << (unsigned long long) bufferNum * (unsigned long long) 
                maxBufferSize + (unsigned long long) bufferSize << " bytes" << flush;
        fwrite(buffer, 1, bufferSize, file);
        bufferNum += 1;
    }

    fclose(file);

    cout << "\nSend ACK for lost data in 5 seconds " << "n";

    thread stdby_thread(sendAck);
    time_stamp start_time = current_time();
    while (elapsed_time(current_time(), start_time) < STDBY_TIME) {
        sleep_for(5000);
    }
    stdby_thread.detach();

    cout << "\nFinished" << "\n";
    return 0;
}