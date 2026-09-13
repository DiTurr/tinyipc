# Shared Memory IPC

A lightweight C++ inter-process communication (IPC) middleware based on **POSIX shared memory** and **Linux futexes**.

The goal is to provide fast communication between processes with minimal overhead, avoiding message serialization and heavyweight middleware layers.

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

The publisher writes messages directly into shared memory. The subscriber accesses the same memory region and uses a Linux futex to efficiently wait for new messages.

The shared memory buffer is organized as a fixed-size ring buffer. Each slot contains its own sequence number, allowing subscribers to detect whether a slot is valid or has been modified while being read.

The middleware follows **latest-message semantics**: a subscriber is not required to receive every published message. If a subscriber is delayed by OS scheduling or otherwise cannot process messages fast enough, it skips intermediate messages and reads the latest available message.

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

`BufferHeader` contains metadata shared between publisher and subscribers.

* `sequence` — identifies the latest published message and is also used as the futex synchronization value.
* `buffer_size` — stores the number of slots in the buffer.

The global sequence number is the source of truth for determining whether a new message is available.

### Slot

Each slot contains:

```text
+---------------------------+
| sequence                  |
| timestamp_us              |
| data                      |
+---------------------------+
```

The slot sequence number is used to validate the contents of the slot.

During publication, the publisher first changes the slot sequence to the new publication sequence. While the header still contains the previous sequence, the slot therefore appears invalid to subscribers.

The publisher then writes the timestamp and data and finally updates the header sequence. Once the header sequence matches the slot sequence, the slot is considered valid for that publication.

A subscriber also checks the slot sequence after copying the data. If the sequence changed during the copy, the slot was reused while it was being read and the subscriber retries with the latest sequence.

### Buffer

`Buffer<T, BufferSize>` contains the header and a fixed-size array of slots:

```cpp
Buffer<T, BufferSize>
```

The publisher defines the message type and buffer size at compile time.

The buffer is used as a ring:

```text
sequence = N

slot index = N % BufferSize
```

Each new publication advances the global sequence and selects the next slot.

## Message Types

Common IPC message types inherit from `BaseType`.

`BaseType` contains a publication timestamp:

```cpp
struct BaseType {
    uint64_t timestamp_us{0};
};
```

The timestamp uses `std::chrono::steady_clock` and is intended for measuring elapsed time and communication latency.

Available primitive message types include:

```text
Int8Type       Int16Type       Int32Type       Int64Type
UInt8Type      UInt16Type      UInt32Type      UInt64Type
Float32Type    Float64Type     BoolType
```

Each type contains a `data` field in addition to the common timestamp.

## Publisher

The publisher creates or opens a POSIX shared-memory object and maps it into its address space.

A publication follows this sequence:

```text
Header sequence = N
        │
        ▼
Select slot N % BufferSize
        │
        ▼
Slot sequence = N + 1
        │
        │  Invalidate slot
        ▼
Write timestamp
        │
        ▼
Write data
        │
        ▼
Header sequence = N + 1
        │
        ▼
FUTEX_WAKE
```

The slot sequence is changed before the data is written so that a subscriber cannot consider the slot valid while it is being modified.

The header sequence is updated only after the slot has been completely written. The header update therefore acts as the publication/notification point.

Example:

```cpp
Publisher<UInt32Type, 8> publisher("/my_topic");

UInt32Type value;
value.data = 42;

publisher.publish(value);
```

## Subscriber

The subscriber opens an existing shared-memory object without requiring the buffer size as a template parameter:

```cpp
Subscriber<UInt32Type> subscriber("/my_topic");

UInt32Type value = subscriber.wait();
```

The subscriber maintains its own last processed sequence number.

The basic operation is:

```text
             ┌───────────────────┐
             │ Load header seq.  │
             └─────────┬─────────┘
                       │
                New message?
                 /           \
               No             Yes
               │               │
               ▼               ▼
       FUTEX_WAIT         Select latest slot
               │               │
               │               ▼
               │        Check slot sequence
               │               │
               │               ▼
               │          Read data
               │               │
               │               ▼
               │        Check sequence again
               │               │
               └───────┐       │
                       │       ▼
                       └─── Return data
```

The subscriber always checks the sequence before calling `FUTEX_WAIT`.

This is important because the publisher may update the sequence between the subscriber's last check and the futex call. The sequence check prevents a missed notification from causing the subscriber to sleep indefinitely.

The futex is therefore used as a **notification mechanism**, while the sequence number represents the actual synchronization state.

## Late Subscriber Wake-Up

A subscriber may not be scheduled immediately after `FUTEX_WAKE`.

For example:

```text
Subscriber sequence = 100

Publisher:
    publish 101 → header = 101 → wake
    publish 102 → header = 102 → wake
    publish 103 → header = 103 → wake

Subscriber finally gets CPU time:
    header = 103
    local   = 100
```

The subscriber does not need to receive the individual wakeups for messages 101 and 102.

It detects:

```text
103 != 100
```

and directly selects the latest published slot.

Therefore, futex wakeups do not represent individual messages. They only indicate that the subscriber should check the shared state again.

This makes the communication robust against scheduling delays and avoids requiring one successful wake-up per message.

## Latest-Message Semantics

The middleware intentionally prioritizes **latest-message delivery** over guaranteed delivery of every message.

If the publisher produces messages faster than a subscriber can process them, intermediate messages may be skipped.

For example, with:

```text
BufferSize = 4
```

a subscriber may observe:

```text
100 → 104 → 108
```

while messages in between were overwritten by newer publications.

This behavior is appropriate for data such as:

* Sensor measurements.
* Perception results.
* Vehicle state.
* Object tracking.
* Control-related data where stale information is less useful than current information.

The ring buffer provides a small history and protects against immediate overwrites, but it is not intended to provide reliable message delivery or queue semantics.

## Synchronization

The synchronization mechanism uses the header sequence together with per-slot sequence numbers:

```text
Publisher
    │
    ├── Select slot
    │
    ├── Invalidate slot
    │
    ├── Write timestamp + data
    │
    ├── Commit slot sequence
    │
    ├── Update header sequence
    │
    └── FUTEX_WAKE
             │
             ▼
        Subscriber
             │
             ├── Check header sequence
             │
             ├── FUTEX_WAIT if unchanged
             │
             ├── Check slot sequence
             │
             ├── Read data
             │
             └── Verify slot sequence
```

The subscriber uses acquire loads when observing the sequence numbers, while the publisher uses release stores for publication.

The sequence checks provide protection against reading a slot that has been reused while the subscriber is accessing it.

## Minimization of OS System Calls

A key design goal is to keep the normal data path in user space and minimize operating-system system calls.

The shared-memory data exchange itself does not require a system call:

```text
Publisher
    │
    ├── write shared memory
    ├── update atomics
    │
    └── wake only when necessary

Subscriber
    │
    ├── check atomic sequence
    ├── read shared memory
    │
    └── wait only when no new data exists
```

The subscriber therefore performs a normal atomic sequence check before entering the kernel.

If a new message is already available, it immediately processes the message without calling `FUTEX_WAIT`.

Only when:

```cpp
header.sequence == sequence_
```

does the subscriber need to block.

Similarly, `FUTEX_WAKE` is used only after a publication to notify potentially waiting subscribers.

This results in:

```text
New message already available
        │
        └── No system call required

No new message
        │
        └── FUTEX_WAIT → block in kernel
                       ↓
                  FUTEX_WAKE
                       ↓
                  continue in user space
```

The sequence number also makes missed or delayed wakeups harmless: the subscriber can always determine the current state by reading shared memory.

## Memory and Copying

The middleware avoids serialization and copies only the message data required by the subscriber.

The publisher writes directly into the shared-memory slot.

The subscriber copies the validated message into its local output object:

```cpp
T value;
subscriber.wait(value);
```

This avoids exposing a shared-memory slot after `wait()` returns, since the publisher is free to reuse the slot for a later publication.

The design therefore provides:

* No serialization/deserialization.
* No dynamic allocation during normal message exchange.
* Direct publisher access to shared memory.
* One subscriber-side data copy.
* No kernel involvement for the actual data transfer.

## Design Goals

The middleware is intended to provide:

* Low-latency process-to-process communication.
* Shared-memory data exchange without serialization.
* Minimal OS system-call overhead.
* Linux futex-based blocking synchronization.
* Latest-message semantics.
* Fixed-size memory allocation.
* Lock-free communication primitives based on atomics.
* Per-slot sequence validation.
* Detection of slot modification during reads.
* A simple Publisher/Subscriber API.
* A fixed-size ring buffer for bounded memory usage.
* Efficient handling of subscribers that are delayed by OS scheduling.

## Current Limitations

The current implementation is intentionally Linux-specific and lightweight.

Important limitations include:

* Linux futexes are used directly through `syscall`.
* The shared-memory object lifecycle and error handling are still minimal.
* The design assumes a single publisher for a given buffer.
* Intermediate messages may be dropped when subscribers are slower than the publisher.
* The subscriber performs one copy from shared memory into its local object.
* The implementation does not provide guaranteed delivery or message acknowledgements.
* Arbitrary non-atomic `T` objects require care because the publisher may reuse a slot while a subscriber is accessing it; the sequence protocol detects modification, but does not by itself make concurrent non-atomic accesses formally race-free under the C++ memory model.
