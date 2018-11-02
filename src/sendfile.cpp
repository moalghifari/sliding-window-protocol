#include <iostream>
#include <thread>
#include <mutex>

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

#include "helpers.h"

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
    struct hostent *dest_hnet;
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

    dest_hnet = gethostbyname(destinationIP); 
    if (!dest_hnet) {
        cerr << "unknown host: " << destinationIP << endl;
        return 1;
    }

    memset(&serverAddress, 0, sizeof(serverAddress)); 
    memset(&clientAddress, 0, sizeof(clientAddress)); 

    serverAddress.sin_family = AF_INET;
    bcopy(dest_hnet->h_addr, (char *)&serverAddress.sin_addr, 
            dest_hnet->h_length); 
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
    int buffer_size;
    thread recv_thread(listenAck);

    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    int frame_size;
    int data_size;

    bool read_done = false;
    int buffer_num = 0;
    while (!read_done) {

        buffer_size = fread(buffer, 1, maxBufferSize, file);
        if (buffer_size == maxBufferSize) {
            char temp[1];
            int next_buffer_size = fread(temp, 1, 1, file);
            if (next_buffer_size == 0) read_done = true;
            int error = fseek(file, -1, SEEK_CUR);
        } else if (buffer_size < maxBufferSize) {
            read_done = true;
        }
        
        windowInfoMutex.lock();

        int seq_count = buffer_size / MAX_DATA_SIZE + ((buffer_size % MAX_DATA_SIZE == 0) ? 0 : 1);
        int seq_num;
        windowSentTime = new time_stamp[windowLength];
        isWindowAcked = new bool[windowLength];
        bool window_sent_mask[windowLength];
        for (int i = 0; i < windowLength; i++) {
            isWindowAcked[i] = false;
            window_sent_mask[i] = false;
        }
        lar = -1;
        lfs = lar + windowLength;

        windowInfoMutex.unlock();

        bool send_done = false;
        while (!send_done) {

            windowInfoMutex.lock();

            if (isWindowAcked[0]) {
                int shift = 1;
                for (int i = 1; i < windowLength; i++) {
                    if (!isWindowAcked[i]) break;
                    shift += 1;
                }
                for (int i = 0; i < windowLength - shift; i++) {
                    window_sent_mask[i] = window_sent_mask[i + shift];
                    isWindowAcked[i] = isWindowAcked[i + shift];
                    windowSentTime[i] = windowSentTime[i + shift];
                }
                for (int i = windowLength - shift; i < windowLength; i++) {
                    window_sent_mask[i] = false;
                    isWindowAcked[i] = false;
                }
                lar += shift;
                lfs = lar + windowLength;
            }

            windowInfoMutex.unlock();

            for (int i = 0; i < windowLength; i ++) {
                seq_num = lar + i + 1;

                if (seq_num < seq_count) {
                    windowInfoMutex.lock();

                    if (!window_sent_mask[i] || (!isWindowAcked[i] && (elapsed_time(current_time(), windowSentTime[i]) > TIMEOUT))) {
                        int buffer_shift = seq_num * MAX_DATA_SIZE;
                        data_size = (buffer_size - buffer_shift < MAX_DATA_SIZE) ? (buffer_size - buffer_shift) : MAX_DATA_SIZE;
                        memcpy(data, buffer + buffer_shift, data_size);
                        
                        bool eot = (seq_num == seq_count - 1) && (read_done);
                        frame_size = create_frame(seq_num, frame, data, data_size, eot);

                        sendto(socketObj, frame, frame_size, 0, 
                                (const struct sockaddr *) &serverAddress, sizeof(serverAddress));
                        window_sent_mask[i] = true;
                        windowSentTime[i] = current_time();
                    }

                    windowInfoMutex.unlock();
                }
            }

            if (lar >= seq_count - 1) send_done = true;
        }

        cout << "\r" << "[SENT " << (unsigned long long) buffer_num * (unsigned long long) 
                maxBufferSize + (unsigned long long) buffer_size << " BYTES]" << flush;
        buffer_num += 1;
        if (read_done) break;
    }
    
    fclose(file);
    delete [] isWindowAcked;
    delete [] windowSentTime;
    recv_thread.detach();

    cout << "\nAll done :)" << endl;
    return 0;
}
