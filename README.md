# Shared Memory IPC

A lightweight C++ inter-process communication (IPC) middleware based on **POSIX shared memory** and **Linux futexes**.

The goal is to provide fast communication between processes without requiring message serialization or a heavyweight middleware layer.

## Description

The middleware provides a simple **Publisher/Subscriber** interface:

```text
             Shared Memory
                  │
        ┌─────────┴─────────┐
        │                   │
    Publisher            Subscriber
        │                   │
     publish()             wait()
```

The publisher writes messages directly into shared memory. The subscriber accesses the same memory region and waits for new messages using a Linux futex.

The current implementation uses **slot 0** for publishing. The buffer already supports multiple slots and is intended to be extended into a ring buffer.

## Architecture

The shared-memory region consists of a header followed by the message slots:

```text
+---------------------------+
| BufferHeader              |
|                           |
|  sequence                 |
|  buffer_size              |
+---------------------------+
| slot[0]                   |
+---------------------------+
| slot[1]                   |
+---------------------------+
| ...                       |
+---------------------------+
| slot[BufferSize - 1]      |
+---------------------------+
```

### BufferHeader

`BufferHeader` contains metadata shared between publisher and subscriber.

* `sequence` — identifies the latest published message and is also used for futex synchronization.
* `buffer_size` — stores the number of slots in the buffer.

### Buffer

`Buffer<T, BufferSize>` contains the header and a fixed-size array of messages:

```cpp
Buffer<T, BufferSize>
```

This allows the publisher to define the message type and buffer size at compile time.

### Message Types

Common IPC message types inherit from `BaseType`.

`BaseType` contains a publication timestamp:

```cpp
struct BaseType {
    uint64_t timestamp_us{0};
};
```

Available primitive message types include:

```text
Int8Type       Int16Type       Int32Type       Int64Type
UInt8Type      UInt16Type      UInt32Type      UInt64Type
Float32Type    Float64Type     BoolType
```

Each type contains a `data` field in addition to the common timestamp.

## Publisher

The publisher creates or opens a POSIX shared-memory object and maps it into its address space.

Messages are currently published to slot 0:

```cpp
Publisher<UInt32Type, 8> publisher("/my_topic");

UInt32Type value;
value.data = 42;

publisher.publish(value);
```

After writing the message, the publisher increments the sequence number and wakes a waiting subscriber using `FUTEX_WAKE`.

## Subscriber

The subscriber opens an existing shared-memory object without requiring the buffer size as a template parameter:

```cpp
Subscriber<UInt32Type> subscriber("/my_topic");

UInt32Type value = subscriber.wait();
```

The subscriber checks the sequence number. If no new message is available, it blocks using `FUTEX_WAIT`.

When the publisher updates the sequence number, the futex wakes the subscriber and the new message is returned.

## Synchronization

The synchronization mechanism is:

```text
Publisher
    │
    ├── Write message
    │
    ├── Increment sequence
    │
    └── FUTEX_WAKE
             │
             ▼
        Subscriber
             │
             ├── FUTEX_WAIT
             │
             ├── Detect new sequence
             │
             └── Read message
```

This avoids continuously polling the CPU while waiting for new data.

## Design Goals

The middleware is intended to provide:

* Low-latency process-to-process communication.
* Zero-copy access to shared memory.
* No message serialization for shared-memory messages.
* Simple Publisher/Subscriber API.
* Fixed-size memory allocation.
* Linux futex-based synchronization.
* A foundation for a future ring-buffer implementation.
