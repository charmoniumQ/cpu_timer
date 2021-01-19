#pragma once
#include <chrono>
#include <ctime>
#include <system_error>

namespace myclock {
	using CpuTime = std::chrono::nanoseconds;
	using WallTime = std::chrono::nanoseconds;

	/**
	 * @brief A C++ translation of [clock_gettime][1]
	 *
	 * [1]: https://linux.die.net/man/3/clock_gettime
	 *
	 */
	static std::chrono::nanoseconds cpp_clock_gettime(clockid_t clock_id) {
		struct timespec ts {};
		if (clock_gettime(clock_id, &ts) != 0) {
			throw std::system_error(std::make_error_code(std::errc(errno)), "clock_gettime");
		}
		return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
	}

	static CpuTime cpu_now() {
		return cpp_clock_gettime(CLOCK_THREAD_CPUTIME_ID);
	}

	static WallTime wall_now() {
		return cpp_clock_gettime(CLOCK_MONOTONIC);
	}

	static size_t get_nanoseconds(CpuTime /*or WallTime*/ t) {
		return t.count();
	}
} // namespace myclock
