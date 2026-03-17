
# 🚀 Concurrency Analyzer (Producer-Consumer in C)

This project demonstrates and compares different concurrency mechanisms in C using the Producer-Consumer problem.

## 📌 Features
- Implements Producer-Consumer using:
  - Threads (pthreads)
  - Shared Memory + Semaphores
  - Pipes (IPC)
- Measures:
  - Throughput (items/sec)
  - Average latency (ns)
  - Shutdown time
- Configurable parameters (buffer size, producers, consumers)

## 🏗️ Project Structure
    .
    ├── concurrency_analyzer.c
    └── README.md

## ⚙️ Working

### 🧵 Threads
- Uses pthreads
- Mutex + condition variables for synchronization

### 🧠 Shared Memory
- Uses mmap()
- POSIX semaphores for synchronization

### 🔗 Pipes
- Uses pipe() and fork()
- Simple IPC between processes

## 📊 Metrics
- Throughput: Items processed per second
- Latency: Time between production and consumption
- Shutdown Time: Time to stop consumers

## 🧪 Configuration
Modify these values in code:
```c
#define BUFFER_SIZE 8192
#define TOTAL_ITEMS 2000000LL
#define N_PRODUCERS 4
#define N_CONSUMERS 4
````

## ▶️ Compilation

```bash
gcc -O2 -pthread main.c -o analyzer
```

## ▶️ Execution

Run all:

```bash
./analyzer all
```

Run specific:

```bash
./analyzer thread
./analyzer shm
./analyzer pipe
```

## 📌 Example Output

```
Concurrency Analyzer (Producer-Consumer)
----------------------------------------

Threads:
 throughput: 5000000 items/sec
 shutdown time: 2.34 ms

Shared memory + semaphores:
 throughput: 4200000 items/sec
 shutdown time: 3.12 ms

Pipes:
 throughput: 1500000 items/sec
 shutdown time: 5.87 ms
```

## ⚠️ Notes

* Threads are fastest (shared memory, low overhead)
* Shared memory is efficient for processes
* Pipes are slower due to kernel communication
* Uses POISON value (-1) for termination

## 🧠 Concepts Covered

* Producer-Consumer Problem
* Multithreading
* Inter-Process Communication (IPC)
* Mutex, Condition Variables, Semaphores

## 📚 Requirements

* GCC compiler
* Linux/Unix (Ubuntu recommended)

## 👩‍💻 Author

Aastha Patel


---

Now this will **render perfectly on GitHub** without any formatting issues 👍
```
