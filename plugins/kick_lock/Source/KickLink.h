#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstring>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <sys/mman.h>
 #include <unistd.h>
#endif

namespace kl
{
/** The link between Kick Lock Send (on the kick's track) and Kick Lock (on
    the bass): no audio routing in the host. The sender publishes the time of
    each kick on the song's grid (quarter notes, from the host's playhead);
    Kick Lock places them on its own grid. A small block of shared memory
    (works across plug-in binaries and processes), lock-free:

        slot (one per sender): owner, heartbeat, name, a ring of events

    Each event is written then published by bumping the slot's count
    (release); a reader takes the events between what it has seen and the
    count (acquire). */
struct KickLinkMemory
{
    static constexpr std::uint32_t magic = 0x4b4c4e4b; // "KLNK"
    static constexpr int version = 1, slots = 8, ringSize = 128, nameSize = 64;

    struct Event
    {
        double ppq = 0.0;       // the kick's position (quarter notes)
        float level = 0.0f;     // its peak (linear)
        float pad = 0.0f;
    };

    struct Slot
    {
        std::atomic<std::uint64_t> owner;        // 0 = free
        std::atomic<std::int64_t> heartbeatMs;   // last sign of life (wall clock)
        std::atomic<std::uint32_t> count;        // events published so far
        std::atomic<std::uint32_t> nameVersion;
        std::atomic<std::int64_t> listenedMs;    // last time a Kick Lock read it
        char name[nameSize];
        Event events[ringSize];
    };

    std::atomic<std::uint32_t> header;
    std::atomic<std::int32_t> layout;
    Slot slot[slots];
};

static_assert (std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<std::int64_t>::is_always_lock_free,
               "the shared block needs address-free atomics");

/** Maps the shared block (created by whichever plug-in comes first). */
class KickLink
{
public:
    /** Tests use their own block (so a running Live is never disturbed). */
    static juce::String& blockName()
    {
        static juce::String name ("lalzin_kicklock_v1");
        return name;
    }

    KickLink()
    {
        const auto name = blockName();
        const size_t size = sizeof (KickLinkMemory);
       #if JUCE_WINDOWS
        mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) size, ("Local\\" + name).toWideCharPointer());
        if (mapping != nullptr)
            memory = static_cast<KickLinkMemory*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, size));
       #else
        const auto shmName = ("/" + name).toStdString();
        const int fd = shm_open (shmName.c_str(), O_RDWR | O_CREAT, 0600);
        if (fd >= 0)
        {
            struct stat st {};
            if (fstat (fd, &st) == 0 && (size_t) st.st_size < size)
                (void) ftruncate (fd, (off_t) size); // fresh blocks are zero-filled
            void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close (fd);
            if (p != MAP_FAILED)
                memory = static_cast<KickLinkMemory*> (p);
        }
       #endif
        if (memory != nullptr)
        {
            // a zero-filled block is valid as it is (all slots free); stamp it
            std::uint32_t expected = 0;
            memory->header.compare_exchange_strong (expected, KickLinkMemory::magic);
            if (memory->header.load() != KickLinkMemory::magic)
                release(); // not ours: no link rather than a crash
        }
    }

    ~KickLink() { release(); }

    /** Tests: removes the block (the next KickLink creates a fresh one). */
    static void removeBlock()
    {
       #if ! JUCE_WINDOWS
        shm_unlink (("/" + blockName()).toRawUTF8());
       #endif
    }

    bool isValid() const noexcept { return memory != nullptr; }
    KickLinkMemory* get() noexcept { return memory; }

    static std::int64_t nowMs() noexcept { return (std::int64_t) juce::Time::currentTimeMillis(); }

private:
    void release()
    {
        if (memory == nullptr)
            return;
       #if JUCE_WINDOWS
        UnmapViewOfFile (memory);
        if (mapping != nullptr)
            CloseHandle (mapping);
        mapping = nullptr;
       #else
        munmap (memory, sizeof (KickLinkMemory));
       #endif
        memory = nullptr;
    }

    KickLinkMemory* memory = nullptr;
   #if JUCE_WINDOWS
    HANDLE mapping = nullptr;
   #endif
    JUCE_DECLARE_NON_COPYABLE (KickLink)
};

/** The sender's side (Kick Lock Send): claims a slot, publishes kicks. */
class KickLinkSender
{
public:
    KickLinkSender() : id (makeId()) {}
    ~KickLinkSender()
    {
        if (auto* m = link.get(); m != nullptr && slotIndex >= 0)
        {
            auto expected = id;
            m->slot[slotIndex].owner.compare_exchange_strong (expected, 0);
        }
    }

    /** Audio thread, once per block: keeps the slot (claims one if needed). */
    void heartbeat() noexcept
    {
        auto* m = link.get();
        if (m == nullptr)
            return;
        const auto now = KickLink::nowMs();
        if (slotIndex < 0 || m->slot[slotIndex].owner.load (std::memory_order_relaxed) != id)
        {
            slotIndex = -1;
            for (int s = 0; s < KickLinkMemory::slots && slotIndex < 0; ++s)
            {
                auto& sl = m->slot[s];
                std::uint64_t owner = sl.owner.load();
                const bool stale = owner != 0 && now - sl.heartbeatMs.load() > 3000;
                if ((owner == 0 || stale) && sl.owner.compare_exchange_strong (owner, id))
                {
                    slotIndex = s;
                    sl.heartbeatMs.store (now);
                    publishName (pendingName);
                }
            }
            if (slotIndex < 0)
                return;
        }
        if (now - lastBeat > 100)
        {
            m->slot[slotIndex].heartbeatMs.store (now, std::memory_order_relaxed);
            lastBeat = now;
        }
    }

    void publish (double ppq, float level) noexcept
    {
        auto* m = link.get();
        if (m == nullptr || slotIndex < 0)
            return;
        auto& sl = m->slot[slotIndex];
        const auto n = sl.count.load (std::memory_order_relaxed);
        auto& e = sl.events[n % KickLinkMemory::ringSize];
        e.ppq = ppq;
        e.level = level;
        sl.count.store (n + 1, std::memory_order_release);
    }

    /** Message thread: the track's name (shown by Kick Lock). */
    void setName (const juce::String& name)
    {
        std::strncpy (pendingName, name.toRawUTF8(), sizeof (pendingName) - 1);
        publishName (pendingName);
    }

    /** UI: is a Kick Lock reading this slot (within the last 2 s)? */
    bool isListenedTo() const noexcept
    {
        auto* m = const_cast<KickLink&> (link).get();
        return m != nullptr && slotIndex >= 0 && KickLink::nowMs() - m->slot[slotIndex].listenedMs.load (std::memory_order_relaxed) < 2000;
    }
    bool isConnected() const noexcept { return link.isValid() && slotIndex >= 0; }

private:
    static std::uint64_t makeId()
    {
        juce::Random r;
        return ((std::uint64_t) (std::uint32_t) r.nextInt() << 32) | (std::uint32_t) r.nextInt() | 1u;
    }
    void publishName (const char* name) noexcept
    {
        auto* m = link.get();
        if (m == nullptr || slotIndex < 0)
            return;
        auto& sl = m->slot[slotIndex];
        std::strncpy (sl.name, name, KickLinkMemory::nameSize - 1);
        sl.name[KickLinkMemory::nameSize - 1] = 0;
        sl.nameVersion.fetch_add (1, std::memory_order_release);
    }

    KickLink link;
    const std::uint64_t id;
    int slotIndex = -1;
    std::int64_t lastBeat = 0;
    char pendingName[KickLinkMemory::nameSize] = "Kick";
};

/** Kick Lock's side: follows the most recent live sender, hands over its new kicks. */
class KickLinkReceiver
{
public:
    /** Audio thread: calls fn (ppq, level) for each kick published since the
        last call; returns the followed slot (-1 = no live sender). */
    template <typename Fn>
    int poll (Fn&& fn) noexcept
    {
        auto* m = link.get();
        if (m == nullptr)
            return -1;
        const auto now = KickLink::nowMs();
        auto live = [&] (int s) { return m->slot[s].owner.load (std::memory_order_relaxed) != 0 && now - m->slot[s].heartbeatMs.load (std::memory_order_relaxed) < 1500; };
        if (followed < 0 || ! live (followed))
        {
            followed = -1;
            for (int s = 0; s < KickLinkMemory::slots; ++s)
                if (live (s))
                {
                    followed = s;
                    seen = m->slot[s].count.load (std::memory_order_acquire); // only the kicks from now on
                    break;
                }
            if (followed < 0)
                return -1;
        }
        auto& sl = m->slot[followed];
        sl.listenedMs.store (now, std::memory_order_relaxed);
        const auto count = sl.count.load (std::memory_order_acquire);
        if (count - seen > (std::uint32_t) KickLinkMemory::ringSize)
            seen = count - KickLinkMemory::ringSize;
        for (; seen != count; ++seen)
        {
            const auto& e = sl.events[seen % KickLinkMemory::ringSize];
            fn (e.ppq, e.level);
        }
        return followed;
    }

    /** UI: the followed sender's name (empty = none). */
    juce::String followedName() const
    {
        auto* m = const_cast<KickLink&> (link).get();
        const int f = followed;
        if (m == nullptr || f < 0)
            return {};
        char buf[KickLinkMemory::nameSize];
        std::memcpy (buf, m->slot[f].name, sizeof (buf));
        buf[sizeof (buf) - 1] = 0;
        return juce::String::fromUTF8 (buf);
    }

private:
    KickLink link;
    int followed = -1;
    std::uint32_t seen = 0;
};
} // namespace kl
