# Multithreaded HTTP Proxy Server with Caching

## Overview
This project implements a multithreaded HTTP proxy server in C using socket programming.
The proxy forwards HTTP requests from clients to remote servers and returns the responses.
It also includes an LRU (Least Recently Used) cache to improve performance for repeated requests.

The server supports multiple concurrent clients using POSIX threads and ensures thread safety
using mutexes and semaphores.

## Features
- Multithreaded client handling using pthreads
- HTTP request parsing and forwarding
- LRU-based caching of HTTP responses
- Concurrent client support with synchronization
- Logs client IP, host, and requested path
- Handles HTTP error responses (400, 403, 404, 500, 501)
- Path normalization for request compatibility

## How It Works
1. Client sends an HTTP request to the proxy.
2. Proxy parses the request and extracts host and path.
3. Proxy checks if the requested URL is present in cache.
   - Cache HIT: Cached response is sent to the client.
   - Cache MISS: Request is forwarded to the remote server.
4. Server response is forwarded to the client and stored in cache.
5. Cache evicts the least recently used entry when size limit is exceeded.

## Technologies Used
- C Programming
- Socket Programming (TCP/IP)
- POSIX Threads
- Semaphores and Mutex Locks
- HTTP Protocol
- LRU Cache Design


## Compilation and Execution

Compile:
gcc proxy.c -o proxy -lpthread

Run:
./proxy <port_number>

Example:
./proxy 8080

## Testing
Test using curl or a browser configured to use the proxy.

Example:
curl -x http://localhost:8080 http://example.com

Terminal output shows:
- Client IP and port
- HTTP request details
- Cache HIT or MISS status

## Limitations
- Supports only HTTP GET requests
- HTTPS (CONNECT method) not supported
- Cache size is limited
- Persistent connections are not implemented

## Key Learnings
- Client-server communication using sockets
- Handling concurrency in network applications
- Implementing an LRU cache
- Thread synchronization using mutexes and semaphores
- Understanding HTTP request-response flow


