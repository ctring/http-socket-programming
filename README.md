# HTTP Socket Programming

Simple HTTP server and client to practice socket programming

## How to compile

Go to the directory containing the Makefile and run

```
make
```

## How to run
### Client

```
./http_client [-p] <host> <port>
```
`-p` - print the RTT for connecting to the host

### Server
```
./http_server <port>
```