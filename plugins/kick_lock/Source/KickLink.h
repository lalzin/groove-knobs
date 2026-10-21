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
    static constexpr int version = 3, slots = 16, ringSize = 128, nameSize = 64, channels = 11, // channels 0..10
                         waveSize = 16384, waveDecimation = 8;                        // the kick's wave, 1 in 8 (~2.7 s at 48 kHz)

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
        std::atomic<std::int32_t> channel;       // LINK 0..10: only the Kick Locks on it listen
        char name[nameSize];
        Event events[ringSize];
        // the kick as it leaves Send (1 in 8), for Kick Lock's scope: the
        // position (quarter notes) of the last one written, and the step
        std::atomic<std::uint32_t> waveCount;
        std::atomic<double> waveEndPpq, waveStepPpq;
        float wave[waveSize];
    };

    std::atomic<std::uint32_t> header;
    std::atomic<std::int32_t> layout;
    Slot slot[slots];
};

static_assert (std::atomic<std::uint64_t>::is_always_lock_free && std::atomic<std::int64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free,
               "the shared block needs address-free atomics");

/** Maps the shared block (created by whichever plug-in comes first). */
class KickLink
{
public:
    /** Tests use their own block (so a running Live is never disturbed). */
    static juce::String& blockName()
    {
        static juce::String name ("lalzin_kicklock_v3"); // v3: channels + the kick's wave (an older plug-in still loaded keeps its own block)
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

    /** LINK channel (0..10), from the parameter. */
    void setChannel (int c) noexcept { channel.store (juce::jlimit (0, KickLinkMemory::channels - 1, c), std::memory_order_relaxed); }
    int getChannel() const noexcept { return channel.load (std::memory_order_relaxed); }

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
        m->slot[slotIndex].channel.store (channel.load (std::memory_order_relaxed), std::memory_order_relaxed);
        if (now - lastBeat > 100)
        {
            m->slot[slotIndex].heartbeatMs.store (now, std::memory_order_relaxed);
            lastBeat = now;
            // another live Send on the same channel: a conflict (the Kick Locks would follow only one)
            int others = 0;
            for (int s2 = 0; s2 < KickLinkMemory::slots; ++s2)
                if (s2 != slotIndex && m->slot[s2].owner.load (std::memory_order_relaxed) != 0
                    && now - m->slot[s2].heartbeatMs.load (std::memory_order_relaxed) < 1500
                    && m->slot[s2].channel.load (std::memory_order_relaxed) == channel.load (std::memory_order_relaxed))
                    ++others;
            sharedWith.store (others, std::memory_order_relaxed);
        }
    }

    /** UI: other live Sends on the same channel (0 = alone, as it should be). */
    int getSharedWith() const noexcept { return sharedWith.load (std::memory_order_relaxed); }

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

    /** Audio thread: one sample of the kick (already decimated) at `ppq`, `step` quarter notes after the previous one. */
    void pushWave (float v, double ppq, double step) noexcept
    {
        auto* m = link.get();
        if (m == nullptr || slotIndex < 0)
            return;
        auto& sl = m->slot[slotIndex];
        const auto n = sl.waveCount.load (std::memory_order_relaxed);
        sl.wave[n % KickLinkMemory::waveSize] = v;
        sl.waveEndPpq.store (ppq, std::memory_order_relaxed);
        sl.waveStepPpq.store (step, std::memory_order_relaxed);
        sl.waveCount.store (n + 1, std::memory_order_release);
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
    std::atomic<int> channel { 1 }, sharedWith { 0 };
    char pendingName[KickLinkMemory::nameSize] = "Kick";
};

/** Kick Lock's side: follows the most recent live sender, hands over its new kicks. */
class KickLinkReceiver
{
public:
    /** Audio thread: calls fn (ppq, level) for each kick published since the
        last call; returns the followed slot (-1 = no live sender). */
    template <typename Fn>
    int poll (int channel, Fn&& fn) noexcept
    {
        auto* m = link.get();
        if (m == nullptr)
            return -1;
        const auto now = KickLink::nowMs();
        auto live = [&] (int s) { return m->slot[s].owner.load (std::memory_order_relaxed) != 0 && now - m->slot[s].heartbeatMs.load (std::memory_order_relaxed) < 1500; };
        auto onChannel = [&] (int s) { return live (s) && m->slot[s].channel.load (std::memory_order_relaxed) == channel; };
        if (followed < 0 || ! onChannel (followed))
        {
            followed = -1;
            for (int s = 0; s < KickLinkMemory::slots; ++s)
                if (onChannel (s))
                {
                    followed = s;
                    seen = m->slot[s].count.load (std::memory_order_acquire); // only the kicks from now on
                    break;
                }
            if (followed < 0)
            {
                sendersOnChannel.store (0, std::memory_order_relaxed);
                return -1;
            }
        }
        if (now - lastCount > 250)
        {
            int n = 0;
            for (int s = 0; s < KickLinkMemory::slots; ++s)
                n += onChannel (s) ? 1 : 0;
            sendersOnChannel.store (n, std::memory_order_relaxed);
            lastCount = now;
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

    /** Audio thread: the followed Send's kick at a position (quarter notes); false if not there (yet). */
    bool waveAt (double ppq, float& value) const noexcept
    {
        auto* m = const_cast<KickLink&> (link).get();
        const int f = followed;
        if (m == nullptr || f < 0)
            return false;
        const auto& sl = m->slot[f];
        const auto n = sl.waveCount.load (std::memory_order_acquire);
        const double end = sl.waveEndPpq.load (std::memory_order_relaxed), step = sl.waveStepPpq.load (std::memory_order_relaxed);
        if (n == 0 || step <= 0.0)
            return false;
        const double back = (end - ppq) / step;
        if (back < -0.5 || back > (double) juce::jmin<std::uint32_t> (n, KickLinkMemory::waveSize - 2))
            return false;
        const auto k = (std::uint32_t) std::llround (juce::jmax (0.0, back));
        value = sl.wave[(n - 1 - k) % KickLinkMemory::waveSize];
        return true;
    }

    /** UI: live Sends on the channel (more than 1: a conflict, only the first is followed). */
    int getSendersOnChannel() const noexcept { return sendersOnChannel.load (std::memory_order_relaxed); }

private:
    KickLink link;
    int followed = -1;
    std::atomic<int> sendersOnChannel { 0 };
    std::int64_t lastCount = 0;
    std::uint32_t seen = 0;
};
} // namespace kl
