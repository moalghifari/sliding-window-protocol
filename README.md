Mochamad Alghifari - 13516038
Ahmad Faiz Sahupala - 13516065
Rifqi Rifaldi Utomo - 13516098

CARA COMPILE :
ketik 'make'

(receive)
./recvfile <filename> <window_size> <buffer_size> <port>

(send)
./sendfile <filename> <window_size> <buffer_size> <ip> <port>

CARA KERJA :
1. SEND
- cek argumen
- cek host
- inisialisasi server & client
- create & bind socket
- open file
- buat thread baru untuk menerima ack
- baca data sebanyak max buffer size
- lalu dikirim tiap framenya
- kalau paket pertama di windownya udah dapet ack, windownya digeser
- kirim paket yang udah timeout

2. RECEIVE
- cek argumen
- inisialisasi server
- create & bind socket
- siapkan file untuk ditulis
- menerimanya dibagi bagi per buffer sizenya
- send ack setelah menerima paket
- kirim ack lagi untuk pengiriman yang hilang atau rusak

PEMBAGIAN KERJA :
Alghi = receive
Faiz = send
Faldi = frame , readme