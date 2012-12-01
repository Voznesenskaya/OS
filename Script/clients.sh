#!/bin/bash
cd
cd Client
gcc client.c -o client

./client "GET /6.html HTTP/1.1 \n" &
./client "GET / HTTP/1.1 \n" &
./client "GET /one.html HTTP/1.1 \n" &
./client "GET /folder/two.html HTTP/1.1\n" &

