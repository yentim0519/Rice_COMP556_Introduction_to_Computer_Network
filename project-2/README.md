# Project 2: Reliable File Transfer Protocol
#### Ting Yen, Min-Hung Shih, Fang-Yu Lin
### Packet format
- A packet is 1472 bytes
  - data_size(unsigned short, 2 bytes): the bytes of meaningful data in the data. 
  - seq_num(unsigned short, 2 bytes): the sequence number of the packet
  - checksum(unsigned short, 2 bytes): the checksum of the packet
  - char data[1466]: Each packet could store upto 1466 bytes in this field.
### Protocals and Algorithms
- The input file could be large(up to 30MB), and we are asked to handle large files using approximately 1 MB memory.
  - sendfile
    - we design a "file_buffer", which is used to store data we read from the file one time. The limit of file_buffer is 1466(bytes) * 690 ~ 1M. 
  - recvfile
    - we have a "recv_buffer", which could store up to 690 packets ~ 1M.
- Communication protocals
  - Both sendfile and recvfile have a sliding window of size 5. 
  - sendfile 
    - Stage 1: Ready to send packets
      - sendfile(client) sends a dummy packet(with seq#65535) to let recvfile(server) know the client is ready to send packets. Once it gets ACK, it will start to send.
    - Stage 2: Send packets
      - First, we send all packets in the sliding window and record the sending time in window_sendtime[].
      - When sendfile gets ACK from the server(recvfile), it will check 1. checksum and 2. sequence number to see whether the ACK is valid:
          - if valid, move the sliding window, and send packets
          - if invalid, resent packets in the sliding window
      - When sendfile receives the ACK for packet(with seq#689) and we still have data to send, the sendfile will load data from the file into file_buffer and go back to Stage 1.
    - Stage 3: Disconnect
      - When all packets were sent and got ACKs, the socket will close.
      
  - recvfile
    - Stage 1: Ready to receive packets
      - When recvfile receives dummy packet, it will reset the sliding window and recv_buffer.
    - Stage 2: Receive packets
      - After getting the first packet, it will open the file(ready to write). If the file not exists, just create it. 
      - When recvfile gets a packet, it will check 1. checksum and 2. sequence number to determine whether the packet is valid:
        - if valid, store the packet in recv_buffer
          - if the packet is valid and seq#A is the start of the sliding window, write data in the sliding window into the file, and send ACK(with seq#A)
        - if invalid, ignore it
      
    - Stage 3: Close the socket
      - If the socket didn't receive anything for 10 secs, the socket will close.
       
### Execution
- run the make file
  - make 
- run the sendfile
  -  sendfile -r <recv_host>:<recv_port> -f <subdir>/<filename>
    - e.g. sendfile -r 128.42.124.180:18001 -f test.txt 
- run the recvfile
  -  recvfile -p <recv port>
    - e.g. recvfile -p 18001

