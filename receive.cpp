#include <iostream>
#include <thread>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <chrono>
#include <stdlib.h> 
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>

#define MAX_DATA_SIZE 1024
#define MAX_FRAME_SIZE 1034
#define ACK_SIZE 6

#define now chrono::high_resolution_clock::now
#define timestamp chrono::high_resolution_clock::time_point
#define differentTime(end, start) chrono::duration_cast<chrono::milliseconds>(end - start).count()
#define sleep_for(x) this_thread::sleep_for(chrono::milliseconds(x));

#define STDBY_TIME 3000

using namespace std;

int socket;
struct sockaddr_in serverAddr, clientAddr;

char checksum(char *frame, int count) {
    u_long sum = 0;
    while (count--) {
        sum += *frame++;
        if (sum & 0xFFFF0000) {
            sum &= 0xFFFF;
            sum++; 
        }
    }
    return (sum & 0xFFFF);
}

bool getFrame(int *seqNum, char *data, int *dataSize, bool *eot, char *frame) {
    *eot = frame[0] == 0x0 ? true : false;

    uint32_t netSeqNum;
    memcpy(&netSeqNum, frame + 1, 4);
    *seqNum = ntohl(netSeqNum);

    uint32_t netDataSize;
    memcpy(&netDataSize, frame + 5, 4);
    *dataSize = ntohl(netDataSize);

    memcpy(data, frame + 9, *dataSize);

    return frame[*dataSize + 9] != checksum(frame, *dataSize + (int) 9);
}

void create_ack(int seqNum, char *ack, bool error) {
    ack[0] = error ? 0x0 : 0x1;
    uint32_t netSeqNum = htonl(seqNum);
    memcpy(ack + 1, &netSeqNum, 4);
    ack[5] = checksum(ack, ACK_SIZE - (int) 1);
}

void sendAck() {
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    int frameSize;
    int dataSize;
    int receiveSeqNum;
    bool frame_error;
    bool eot;
    socklen_t clientAddrSize;

    /* Listen for frames and send ack */
    while (true) {
        frameSize = recvfrom(socket, (char *)frame, MAX_FRAME_SIZE, 
                MSG_WAITALL, (struct sockaddr *) &clientAddr, 
                &clientAddrSize);
        frame_error = getFrame(&receiveSeqNum, data, &dataSize, &eot, frame);

        create_ack(receiveSeqNum, ack, frame_error);
        sendto(socket, ack, ACK_SIZE, 0, 
                (const struct sockaddr *) &clientAddr, clientAddrSize);
    }
}

int main(int argc, char * argv[]) {
    int port;
    int lengthOfWindow;
    int maxBufferSize;
    char *filename;

    //cek argumen
    if (argc == 5) {
        filename = argv[1];
        lengthOfWindow = (int) atoi(argv[2]);
        maxBufferSize = MAX_DATA_SIZE * (int) atoi(argv[3]);
        port = atoi(argv[4]);
    } else {
        cout << "wrong argument" << endl;
        return 1;
    }
      
    // Init server
    memset(&serverAddr, 0, sizeof(serverAddr)); 
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; 
    serverAddr.sin_port = htons(port);

    memset(&clientAddr, 0, sizeof(clientAddr)); 

    // Create & bind socket 
    if ((socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cout << "socket failed" << endl;
        return 1;
    }
    if (::bind(socket, (const struct sockaddr *)&serverAddr, 
            sizeof(serverAddr)) < 0) { 
        count << "socket binding error" << endl;
        return 1;
    }

    FILE *file = fopen(filename, "wb");
    char buffer[maxBufferSize];
    int bufferSize;

    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    int frameSize;
    int dataSize;
    int lfr, laf;
    int receiveSeqNum;
    bool eot;
    bool frame_error;

    //dapetin data
    bool receive = true;
    int bufferNum = 0;
    while (receive) {
        bufferSize = maxBufferSize;
        memset(buffer, 0, bufferSize);
    
        int receiveSeqCount = (int) maxBufferSize / MAX_DATA_SIZE;
        bool windowReceive[lengthOfWindow];
        for (int i = 0; i < lengthOfWindow; i++) {
            windowReceive[i] = false;
        }
        lfr = -1;
        laf = lfr + lengthOfWindow;
        
        /* Receive current buffer with sliding window */
        while (true) {
            socklen_t clientAddrSize;
            frameSize = recvfrom(socket, (char *) frame, MAX_FRAME_SIZE, 
                    MSG_WAITALL, (struct sockaddr *) &clientAddr, 
                    &clientAddrSize);
            frame_error = getFrame(&receiveSeqNum, data, &dataSize, &eot, frame);

            create_ack(receiveSeqNum, ack, frame_error);
            sendto(socket, ack, ACK_SIZE, 0, 
                    (const struct sockaddr *) &clientAddr, clientAddrSize);

            if (receiveSeqNum <= laf) {
                if (!frame_error) {
                    int bufferShift = receiveSeqNum * MAX_DATA_SIZE;

                    if (receiveSeqNum == lfr + 1) {
                        memcpy(buffer + bufferShift, data, dataSize);

                        int shift = 1;
                        for (int i = 1; i < lengthOfWindow; i++) {
                            if (!windowReceive[i]) break;
                            shift += 1;
                        }
                        for (int i = 0; i < lengthOfWindow - shift; i++) {
                            windowReceive[i] = windowReceive[i + shift];
                        }
                        for (int i = lengthOfWindow - shift; i < lengthOfWindow; i++) {
                            windowReceive[i] = false;
                        }
                        lfr += shift;
                        laf = lfr + lengthOfWindow;
                    } else if (receiveSeqNum > lfr + 1) {
                        if (!windowReceive[receiveSeqNum - (lfr + 1)]) {
                            memcpy(buffer + bufferShift, data, dataSize);
                            windowReceive[receiveSeqNum - (lfr + 1)] = true;
                        }
                    }

                    if (eot) {
                        bufferSize = bufferShift + dataSize;
                        receiveSeqCount = receiveSeqNum + 1;
                        receive = false;
                    }
                }
            }
            
            if (lfr >= receiveSeqCount - 1) break;
        }

        cout << "\r" << "Receive " << (unsigned long long) bufferNum * (unsigned long long) 
                maxBufferSize + (unsigned long long) bufferSize << " bytes" << endl;
        fwrite(buffer, 1, bufferSize, file);
        bufferNum += 1;
    }

    fclose(file);

    thread stdby_thread(sendAck);
    timestamp start_time = now();
    while (differentTime(now(), start_time) < STDBY_TIME) {
        sleep_for(400);
    }
    stdby_thread.detach();

    return 0;
}