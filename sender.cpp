#include <iostream>
#include <thread>
#include <mutex>
#include <stdio.h>
#include <unistd.h>
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
#define TIMEOUT 10

#define now chrono::high_resolution_clock::now
#define timestamp chrono::high_resolution_clock::time_point
#define differentTime(end, start) chrono::duration_cast<chrono::milliseconds>(end - start).count()
using namespace std;


// Inisiasi variabel global
int socket;
int lengthOfWindow;
int lar, lfs;
bool *windowAck;
struct sockaddr_in serverAddr, clientAddr;
timestamp TMIN = now();
timestamp *windowTime;
mutex windowInfoMutex;

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

bool getAck(int *seqNum, bool *notSended, char *ack, bool *error) {
    uint32_t netSeqNum;
    memcpy(&netSeqNum, ack + 1, 4);
    *seqNum = ntohl(netSeqNum);
    *notSended = ack[0] == 0x0 ? true : false;
    *error = ack[5] != checksum(ack, ACK_SIZE - (int) 1);
}

void receiveAck() {
    int ackSize;
    int ackSeqNum;
    bool error;
    bool notSended;
    char ack[ACK_SIZE];

    while (true) {
        socklen_t serverAddrSize;
        ack_size = recvfrom(socket, (char *)ack, ACK_SIZE, MSG_WAITALL, (struct sockaddr *) &serverAddr, &serverAddrSize);
        getAck(&ackSeqNum, &notSended, ack, &error);
        windowInfoMutex.lock();

        if (!error && ackSeqNum > lar && ackSeqNum <= lfs) {
            if (!notSended) {
                windowAck[ackSeqNum - (lar + 1)] = true;
            } else {
                windowTime[ackSeqNum - (lar + 1)] = TMIN;
            }
        }

        windowInfoMutex.unlock();
    }
}

int main(int argc, char *argv[]) {
    char *ipDestionation;
    int portDestination;
    int maxBufferSize;
    struct hostent *hostentDestination;
    char *filename;

    // cek argumen
    if (argc != 6) {
    	cout << "wrong argument" << endl;
    	return 1;
    } else {
        filename = argv[1];
        lengthOfWindow = atoi(argv[2]);
        maxBufferSize = MAX_DATA_SIZE * (int) atoi(argv[3]);
        ipDestionation = argv[4];
        portDestination = atoi(argv[5]);
    }

    // cek host
    hostentDestination = gethostbyname(ipDestionation); 
    if (!hostentDestination) {
        cout << "host not detected" << endl;
        return 1;
    }

    //init server
    memset(&serverAddr, 0, sizeof(serverAddr)); 
    serverAddr.sin_family = AF_INET;
    bcopy(hostentDestination->h_addr, (char *)&serverAddr.sin_addr, 
            hostentDestination->h_length); 
    serverAddr.sin_port = htons(portDestination);

    //init client
    memset(&clientAddr, 0, sizeof(clientAddr)); 
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = INADDR_ANY; 
    clientAddr.sin_port = htons(0);

    // Create & bind socket
    if ((socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cout << "socket failed" << endl;
        return 1;
    }
    if (::bind(socket, (const struct sockaddr *)&clientAddr, sizeof(clientAddr)) < 0) { 
        cout << "socket binding error" << endl;
        return 1;
    }

    // urus file
    if (access(filename, F_OK) == -1) {
        cout << "file doesn't exist " << endl;
        return 1;
    }
    FILE *file = fopen(filename, "rb");


    char buffer[maxBufferSize];
    int bufferSize;

    thread recv_thread(listen_ack);

    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    int frameSize;
    int dataSize;

    // kirim data
    bool read = true;
    int bufferNum = 0;
    while (read) {

        // baca perbagian 
        bufferSize = fread(buffer, 1, maxBufferSize, file);
        if (bufferSize == maxBufferSize) {
            char temp[1];
            int nextBufferSize = fread(temp, 1, 1, file);
            if (nextBufferSize == 0) read = false;
            int error = fseek(file, -1, SEEK_CUR);
        } else if (bufferSize < maxBufferSize) {
            read = false;
        }
        
        windowInfoMutex.lock();

        int seqCount = bufferSize / MAX_DATA_SIZE + ((bufferSize % MAX_DATA_SIZE == 0) ? 0 : 1);
        int seqNum;
        windowTime = new timestamp[lengthOfWindow];
        windowAck = new bool[lengthOfWindow];
        bool windowSended[lengthOfWindow];
        for (int i = 0; i < lengthOfWindow; i++) {
            windowAck[i] = false;
            windowSended[i] = false;
        }
        lar = -1;
        lfs = lar + lengthOfWindow;

        windowInfoMutex.unlock();

		// kirim buffeer yg udah ada        
        bool send = true;
        while (send) {

            windowInfoMutex.lock();

            // kalau paket yg pertama udah dapet ack, nanti di pindah
            if (windowAck[0]) {
                int shift = 1;
                for (int i = 1; i < lengthOfWindow; i++) {
                    if (!windowAck[i]) break;
                    shift += 1;
                }
                for (int i = 0; i < lengthOfWindow - shift; i++) {
                    windowSended[i] = windowSended[i + shift];
                    windowAck[i] = windowAck[i + shift];
                    windowTime[i] = windowTime[i + shift];
                }
                for (int i = lengthOfWindow - shift; i < lengthOfWindow; i++) {
                    windowSended[i] = false;
                    windowAck[i] = false;
                }
                lar += shift;
                lfs = lar + lengthOfWindow;
            }

            windowInfoMutex.unlock();

            // kirim frame yg belum dikirim/timeout
            for (int i = 0; i < lengthOfWindow; i ++) {
                seqNum = lar + i + 1;

                if (seqNum < seqCount) {
                    windowInfoMutex.lock();

                    if (!windowSended[i] || (!windowAck[i] && (differentTime(now(), windowTime[i]) > TIMEOUT))) {
                        int buffer_shift = seqNum * MAX_DATA_SIZE;
                        dataSize = (bufferSize - buffer_shift < MAX_DATA_SIZE) ? (bufferSize - buffer_shift) : MAX_DATA_SIZE;
                        memcpy(data, buffer + buffer_shift, dataSize);
                        
                        bool eot = (seqNum == seqCount - 1) && (!read);
                        frameSize = create_frame(seqNum, frame, data, dataSize, eot);

                        sendto(socket, frame, frameSize, 0, 
                                (const struct sockaddr *) &serverAddr, sizeof(serverAddr));
                        windowSended[i] = true;
                        windowTime[i] = now();
                    }

                    windowInfoMutex.unlock();
                }
            }

            if (lar >= seqCount - 1) send = false;
        }

        cout << "\r" << "Send " << (unsigned long long) bufferNum * (unsigned long long) 
                maxBufferSize + (unsigned long long) bufferSize << " bytes" << endl;
        bufferNum += 1;
    }
    
    fclose(file);
    delete [] windowAck;
    delete [] windowTime;
    recv_thread.detach();
    return 0;
}