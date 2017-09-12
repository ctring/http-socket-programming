======= How to compile =======

Go to the directory containing the Makefile and run
    
    make

======= How to run =======

Client
    
    ./http_client [-p] <host> <port>

With `-p` option, the RTT for connecting to the host will be displayed.

Example:
    ./http_client -p www.google.com 80

Server
    ./http_server <port>

Example:
    ./http_server 9999

