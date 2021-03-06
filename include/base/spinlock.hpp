#pragma once

#include <atomic>
#include <cstring>

namespace numa {

template <size_t INITIAL = 16, size_t MAXBKOFF = (1<<10)>
struct ExponentialBackOff {
	size_t curr;
	ExponentialBackOff() : curr(INITIAL) {}

	inline bool operator()() { return backoff(); }

	inline bool backoff() {
		for (size_t i = 0; i < curr; i++)
			asm("pause");
		if (curr < MAXBKOFF) {
			curr <<= 1;
			return true;
		}
		return false;
	}

	inline void reset() {
		curr = INITIAL;
	}
};

template <size_t STEP = 32, size_t MAX = 1024>
struct LinearBackOff {
	size_t curr;
	LinearBackOff() : curr(STEP) {}

	inline bool operator()() { return backoff(); }

	inline bool backoff() {
		for (size_t i = 0; i < curr; i++)
			asm("pause");
		if (curr < MAX) {
			curr += STEP;
			return true;
		}
		return false;
	}

	inline void reset() {
		curr = STEP;
	}
};

#ifndef NUMA_PROFILE_SPINLOCK
#define NUMA_PROFILE_SPINLOCK 0
#endif

template <class BKOFF>
class SpinLockType {
	std::atomic_flag locked = ATOMIC_FLAG_INIT;
	BKOFF bkoff;

#if NUMA_PROFILE_SPINLOCK
	uint64_t counter = 0;

	static size_t rdtsc() {
		uint_least32_t hi, lo;
		__asm__ volatile("rdtsc": "=a"(lo), "=d"(hi));
		return (size_t)lo | ((size_t)hi << 32);
	}
#endif

public:
	SpinLockType() {
		memset((void*) this, 0, sizeof(*this)); // nullify memory for fun
	}

	void lock() {
#if NUMA_PROFILE_SPINLOCK
		size_t t1 = rdtsc();
#endif

		while (locked.test_and_set()) {
			bkoff.backoff();
		}
		bkoff.reset();

#if NUMA_PROFILE_SPINLOCK
		counter += rdtsc() - t1;
#endif
	}
	
	bool try_lock() {
		while (locked.test_and_set()) {
			if (!bkoff.backoff()) return false;
		}
		bkoff.reset();
		return true;
	}
	
	void unlock() {
#if NUMA_PROFILE_SPINLOCK
		size_t t1 = rdtsc();
#endif
		locked.clear();
#if NUMA_PROFILE_SPINLOCK
		counter += rdtsc() - t1;
#endif
	}

	inline size_t count() const {
#if NUMA_PROFILE_SPINLOCK
		return counter;
#else
		return 0;
#endif
	}
};

typedef SpinLockType<ExponentialBackOff<16,1<<10>> SpinLock;

}
