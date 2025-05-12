# Boost.Asio TCP Client-Server

A high-performance TCP client-server application implemented in C++ using the Boost.Asio library. This project demonstrates asynchronous network programming with low-latency message exchange.

## Features

- **Asynchronous I/O**: Fully non-blocking implementation using Boost.Asio for maximum performance
- **Multi-threaded Server**: Server utilizes all available CPU cores for concurrent client handling
- **Low-latency Communication**: Measures and reports message round-trip times in nanoseconds
- **Reliable Message Delivery**: Acknowledgment-based protocol ensures message receipt
- **Thread-safe Message Storage**: Server safely stores client messages with mutex protection

## System Requirements

- C++11 compatible compiler (GCC, Clang, MSVC)
- Boost libraries (1.66 or newer recommended)
- CMake (3.10 or newer) for building

## Building the Project

```bash
# Create build directory
mkdir build && cd build

# Generate build files
cmake ..

# Build the project
cmake --build .
```

## Usage

### Starting the Server

```bash
./server
```

The server will listen on port 8080 and handle multiple client connections concurrently.

### Running the Client

```bash
./client
```

The client will connect to the server at 127.0.0.1:8080 and send 10 messages by default.

When prompted, enter message content to send to the server. For each message, the client will display:
- Message ID
- Timestamp (in nanoseconds)
- Round-trip latency (in nanoseconds)

## Protocol Details

Messages between client and server follow this binary format:
- 8 bytes: Message unique ID
- 8 bytes: Timestamp (nanoseconds since epoch)
- 112 bytes: Message payload

Total message size: 128 bytes

The server responds with a simple "ACK" text acknowledgment for each message.

## Performance Considerations

- The server uses a thread pool matching the number of available CPU cores
- Message handling is fully asynchronous to maximize throughput
- Memory management uses shared pointers for safe object lifetime handling
- Mutex protection prevents data races when storing client messages

## Customization

To modify the default settings:
- Change server port: Update the port number in `tcp_server` constructor in `server.cpp`
- Change client connection target: Modify host and port in `main()` function of `client.cpp`
- Adjust message count: Modify `num_messages` value in `client.cpp`

