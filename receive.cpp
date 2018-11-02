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
        frame_error = read_frame(&receiveSeqNum, data, &dataSize, &eot, frame);

        create_ack(receiveSeqNum, ack, frame_error);
        sendto(socket, ack, ACK_SIZE, 0, 
                (const struct sockaddr *) &clientAddr, clientAddrSize);
    }
}

int main(int argc, char * argv[]) {
    int port;
    int window_len;
    int max_buffer_size;
    char *fname;

    if (argc == 5) {
        fname = argv[1];
        window_len = (int) atoi(argv[2]);
        max_buffer_size = MAX_DATA_SIZE * (int) atoi(argv[3]);
        port = atoi(argv[4]);
    } else {
        cerr << "usage: ./recvfile <filename> <window_size> <buffer_size> <port>" << endl;
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr)); 
    memset(&clientAddr, 0, sizeof(clientAddr)); 
      
    /* Fill server address data structure */
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; 
    serverAddr.sin_port = htons(port);

    /* Create socket file descriptor */ 
    if ((socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        cerr << "socket creation failed" << endl;
        return 1;
    }

    /* Bind socket to server address */
    if (::bind(socket, (const struct sockaddr *)&serverAddr, 
            sizeof(serverAddr)) < 0) { 
        cerr << "socket binding failed" << endl;
        return 1;
    }

    FILE *file = fopen(fname, "wb");
    char buffer[max_buffer_size];
    int buffer_size;

    /* Initialize sliding window variables */
    char frame[MAX_FRAME_SIZE];
    char data[MAX_DATA_SIZE];
    char ack[ACK_SIZE];
    int frameSize;
    int dataSize;
    int lfr, laf;
    int receiveSeqNum;
    bool eot;
    bool frame_error;

    /* Receive frames until EOT */
    bool recv_done = false;
    int buffer_num = 0;
    while (!recv_done) {
        buffer_size = max_buffer_size;
        memset(buffer, 0, buffer_size);
    
        int recv_seq_count = (int) max_buffer_size / MAX_DATA_SIZE;
        bool window_recv_mask[window_len];
        for (int i = 0; i < window_len; i++) {
            window_recv_mask[i] = false;
        }
        lfr = -1;
        laf = lfr + window_len;
        
        /* Receive current buffer with sliding window */
        while (true) {
            socklen_t clientAddrSize;
            frameSize = recvfrom(socket, (char *) frame, MAX_FRAME_SIZE, 
                    MSG_WAITALL, (struct sockaddr *) &clientAddr, 
                    &clientAddrSize);
            frame_error = read_frame(&receiveSeqNum, data, &dataSize, &eot, frame);

            create_ack(receiveSeqNum, ack, frame_error);
            sendto(socket, ack, ACK_SIZE, 0, 
                    (const struct sockaddr *) &clientAddr, clientAddrSize);

            if (receiveSeqNum <= laf) {
                if (!frame_error) {
                    int buffer_shift = receiveSeqNum * MAX_DATA_SIZE;

                    if (receiveSeqNum == lfr + 1) {
                        memcpy(buffer + buffer_shift, data, dataSize);

                        int shift = 1;
                        for (int i = 1; i < window_len; i++) {
                            if (!window_recv_mask[i]) break;
                            shift += 1;
                        }
                        for (int i = 0; i < window_len - shift; i++) {
                            window_recv_mask[i] = window_recv_mask[i + shift];
                        }
                        for (int i = window_len - shift; i < window_len; i++) {
                            window_recv_mask[i] = false;
                        }
                        lfr += shift;
                        laf = lfr + window_len;
                    } else if (receiveSeqNum > lfr + 1) {
                        if (!window_recv_mask[receiveSeqNum - (lfr + 1)]) {
                            memcpy(buffer + buffer_shift, data, dataSize);
                            window_recv_mask[receiveSeqNum - (lfr + 1)] = true;
                        }
                    }

                    /* Set max sequence to sequence of frame with EOT */ 
                    if (eot) {
                        buffer_size = buffer_shift + dataSize;
                        recv_seq_count = receiveSeqNum + 1;
                        recv_done = true;
                    }
                }
            }
            
            /* Move to next buffer if all frames in current buffer has been received */
            if (lfr >= recv_seq_count - 1) break;
        }

        cout << "\r" << "[RECEIVED " << (unsigned long long) buffer_num * (unsigned long long) 
                max_buffer_size + (unsigned long long) buffer_size << " BYTES]" << flush;
        fwrite(buffer, 1, buffer_size, file);
        buffer_num += 1;
    }

    fclose(file);

    /* Start thread to keep sending requested ack to sender for 3 seconds */
    thread stdby_thread(send_ack);
    timestamp start_time = now();
    while (differentTime(now(), start_time) < STDBY_TIME) {
        cout << "\r" << "[STANDBY TO SEND ACK FOR 3 SECONDS | ]" << flush;
        sleep_for(100);
        cout << "\r" << "[STANDBY TO SEND ACK FOR 3 SECONDS / ]" << flush;
        sleep_for(100);
        cout << "\r" << "[STANDBY TO SEND ACK FOR 3 SECONDS - ]" << flush;
        sleep_for(100);
        cout << "\r" << "[STANDBY TO SEND ACK FOR 3 SECONDS \\ ]" << flush;
        sleep_for(100);
    }
    stdby_thread.detach();

    cout << "\nAll done :)" << endl;
    return 0;
}