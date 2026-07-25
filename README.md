# Linux Resource Monitoring System

> A distributed Linux resource monitoring system written entirely in C that collects real-time system metrics from multiple machines, stores them in SQLite, and visualizes them through a live web dashboard.

## Overview

This project implements a lightweight monitoring platform inspired by tools such as `htop`, `top`, and modern monitoring stacks.

It consists of:

- A monitoring agent that collects Linux system metrics.
- A multithreaded monitoring server.
- A producer-consumer queue for asynchronous processing.
- A SQLite database for metric storage.
- A REST API written from scratch.
- A real-time web dashboard for visualization.

The entire backend is implemented in C using POSIX threads, TCP sockets and SQLite without external frameworks.

---

## Features

- Real-time CPU monitoring
- Memory usage monitoring
- Disk usage monitoring
- Network throughput monitoring
- Process monitoring
- Multi-host support
- REST API
- SQLite persistent storage
- Multithreaded server architecture
- Thread-safe producer-consumer queue
- Live dashboard with charts
- Cross-machine monitoring over TCP sockets

---

## System Architecture

```

            +------------------+
            | Monitor Agent #1 |
            +------------------+
                      |
                      |
                   TCP Socket
                      |
            +------------------+
            |                  |
            | Monitor Server   |
            |                  |
            +------------------+
                      |
              Producer Threads
                      |
                      v
              Thread-safe Queue
                      |
                Worker Thread Pool
                      |
                      v
                  SQLite
                      |
                  REST API
                      |
                      v
              Web Dashboard
                      |
               Live Monitoring

```

---

## Project Structure

```text
resource-monitor/
│
├── agent/
│   └── agent.c
│
├── dashboard/
│   ├── index.html
│   ├── style.css
│   └── app.js
│
├── server/
│   ├── include/
│   │
│   └── src/
│       ├── main.c
│       ├── tcp_server.c
│       ├── worker.c
│       ├── queue.c
│       ├── db.c
│       ├── json_parse.c
│       └── http_server.c
│
├── monitor_server
├── monitor_agent
├── monitor.db
├── Makefile
└── README.md

```

---

## Technologies Used

### Languages

- C
- JavaScript
- HTML
- CSS

### Libraries

- POSIX Threads (pthread)
- BSD Sockets
- SQLite3
- Chart.js

### Linux Components

- `/proc/stat`
- `/proc/meminfo`
- `/proc/net/dev`
- `/proc/<pid>/stat`
- `/proc/<pid>/status`
- `statvfs()`

---

## How It Works

### Monitor Agent

The monitoring agent periodically collects:

- CPU usage
- Memory usage
- Disk usage
- Network statistics
- Running processes
- Top CPU consuming processes

The collected metrics are serialized into JSON and sent to the monitoring server through TCP sockets.

```
Agent
 |
 |----CPU
 |----Memory
 |----Disk
 |----Network
 |----Processes
 |
 v
JSON Packet
 |
 v
TCP Socket
 |
 v
Monitoring Server

```

---

### Monitoring Server

The server uses:

- Thread-per-client TCP connections
- Producer-consumer architecture
- Worker thread pool
- SQLite database storage
- HTTP server implementation
- REST API endpoints

Incoming metrics are pushed into a bounded queue and processed asynchronously by worker threads.

```
TCP Connections
       |
       v
Producer Threads
       |
       v
Thread-safe Queue
       |
       v
Worker Threads
       |
       v
SQLite Database
       |
       v
REST API
       |
       v
Dashboard

```

---

## Building

### Ubuntu

```bash
sudo apt install build-essential
sudo apt install libsqlite3-dev
```

Compile the project:

```bash
make
```

Generated binaries:

```bash
monitor_server

monitor_agent
```

---

## Running the Project

### Start the Server

```bash
./monitor_server
```

Output:

```bash
[main] database ready
[main] started 4 consumer worker threads
[tcp] listening on port 5555
[http] dashboard available on port 8080
```

---

### Start an Agent

```bash
./monitor_agent 127.0.0.1 5555 2
```

Arguments:

```text
HOST
PORT
SAMPLING_INTERVAL
```

Example:

```bash
./monitor_agent localhost 5555 2
```

which means:

```text
localhost
port 5555
sample every 2 seconds
```

---

## Dashboard

The dashboard provides:

- CPU utilization
- Memory utilization
- Disk usage
- Network throughput
- Top running processes
- Historical statistics
- Multi-host selection

Open:

```text
http://localhost:8080
```

---

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/hosts | Available hosts |
| GET | /api/latest | Latest metrics |
| GET | /api/history | Historical metrics |
| GET | /api/processes | Top running processes |

Examples:

```bash
/api/hosts

/api/latest?host=my-machine

/api/history?host=my-machine

/api/processes?host=my-machine
```

---

## Software Engineering Concepts Demonstrated

This project demonstrates:

- Linux Systems Programming
- Multithreading
- TCP/IP Networking
- Producer-Consumer Pattern
- REST API Design
- Database Management
- Synchronization using Mutexes and Condition Variables
- Queue-based Architectures
- Concurrent Programming
- Real-time Monitoring Systems
- Web Dashboard Development
- Distributed Systems Fundamentals

---

## Future Improvements

Possible extensions include:

- Docker deployment
- WebSocket support
- Alert notifications
- Authentication system
- Prometheus integration
- Grafana support
- SSL/TLS encryption
- Epoll-based TCP server
- Container monitoring
- Remote deployment support

---

## Screenshot

<img width="1835" height="926" alt="Dashboard" src="https://github.com/user-attachments/assets/9e074b4a-5e1c-4437-b185-772073828893" />


```
dashboard.png
```

---

## Author

**Nada Rachidi**

- Embedded Systems and Artificial Intelligence Engineering Student
- ENSA Fès

---

## License

This project is released under the MIT License.
