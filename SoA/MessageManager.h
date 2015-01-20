#pragma once
#include <unordered_map>

#include "concurrentqueue.h"

enum class MessageID {
    NONE,
    DONE,
    QUIT,
    NEW_PLANET,
    CHUNK_MESH,
    PARTICLE_MESH,
    PHYSICS_BLOCK_MESH
};

class Message
{
public:
    Message() : id(MessageID::NONE), data(NULL){}
    Message(MessageID i, void *d) : id(i), data(d) {}
    MessageID id;
    void *data;
};

enum class ThreadId {
    UPDATE,
    RENDERING
};

inline nString threadIdToString(ThreadId id) {
    switch (id) {
        case ThreadId::UPDATE:
            return "UPDATE";
        case ThreadId::RENDERING:
            return "RENDERING";
    }
}

class MessageManager
{
public:
    MessageManager();
    ~MessageManager();

    /// Enqueues a messages
    /// @param thread: The current thread name
    /// @param message: The message to enqueue
    void enqueue(ThreadId thread, Message message);

    /// Tries to dequeue a message
    /// @param thread: The current thread name
    /// @param result: The resulting message to store
    /// returns true if a message was dequeued, false otherwise
    bool tryDeque(ThreadId thread, Message& result);

    /// Tries to dequeue a buffer of messages
    /// @param thread: The current thread name
    /// @param messageBuffer: The resulting messages to store
    /// @param maxSize: size of the message buffer
    /// returns the number of elements dequeued
    size_t tryDequeMultiple(ThreadId thread, Message* messageBuffer, size_t maxSize);

    /// Waits for a specific message, discarding all other messages
    /// it will return when idToWait is found, or MessageID::QUIT is found
    /// @param thread: The current thread name
    /// @param idToWait: The message ID to wait for
    /// @param result: The resulting message to store
    void waitForMessage(ThreadId thread, MessageID idToWait, Message& result);

    // 1 way lock-free queues.
    moodycamel::ConcurrentQueue <Message> physicsToRender;
    moodycamel::ConcurrentQueue <Message> renderToPhysics;
};

