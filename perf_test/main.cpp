#include "charmonium/scope_timer.hpp"
#include <deque>
#include <functional>
#include <stdexcept>
#include <thread>

namespace ch_sc = charmonium::scope_timer;

static int64_t exec_in_thread(const std::function<void()>& body) {
	ch_sc::CpuNs start {0};
	ch_sc::CpuNs stop  {0};
	std::thread th {[&] {
		ch_sc::detail::fence();
		start = ch_sc::wall_now();
		ch_sc::detail::fence();
		body();
		ch_sc::detail::fence();
		stop = ch_sc::wall_now();
		ch_sc::detail::fence();
	}};
	th.join();
	return ch_sc::detail::get_ns(stop - start);
}

constexpr size_t PAYLOAD_ITERATIONS = 1024;
static void noop() {
	for (size_t i = 0; i < PAYLOAD_ITERATIONS; ++i) {
		// NOLINTNEXTLINE(hicpp-no-assembler)
		asm ("" : /* ins */ : /* outs */ : "memory");
	}
}

class NoopCallback : public ch_sc::CallbackType {
public:
	void thread_start(ch_sc::Thread&) override { noop(); }
	void thread_in_situ(ch_sc::Thread&) override { noop(); }
	void thread_stop(ch_sc::Thread&) override { noop(); }
};

static void fn_no_timing() {
	noop();
}

static void fn_timing() {
	SCOPE_TIMER();
	noop();
}

static void fn_thready_no_timing() {
	exec_in_thread(fn_no_timing);
}

static void fn_thready_timing() {
	exec_in_thread(fn_timing);
}

static int64_t rdtsc() {
	uint32_t lo = 0;
	uint32_t hi = 0;
	// NOLINTNEXTLINE(hicpp-no-assembler)
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	// NOLINTNEXTLINE(hicpp-signed-bitwise)
	return static_cast<int64_t>(hi) << std::numeric_limits<uint32_t>::digits | lo;
}

/*
 * When testing the timers, I will still call noop().
 * The deviation of noop() is relatively low, so it does not impact perfomance that much.
 * On the other hand, by subtracting the time of `for() { noop(); }`, I can cancel out the for-loop and func-call overhead.
 */
static void check_wall() {
	noop();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	auto wall  = ch_sc::wall_now();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	(void)(wall);
}

static void check_cpu() {
	noop();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	auto cpu  = ch_sc::cpu_now();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	(void)(cpu);
}

static void check_tsc() {
	noop();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	auto tsc  = rdtsc();
	if (ch_sc::detail::use_fences) { ch_sc::detail::fence(); }
	(void)(tsc);
}

int main() {
	constexpr int64_t TRIALS = 1024 * 32;

	ch_sc::Process& process = ch_sc::get_process();

	process.set_callback(std::unique_ptr<ch_sc::CallbackType>{new NoopCallback});

	exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_no_timing();
		}
	});

	int64_t time_none = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_no_timing();
		}
	});

	process.set_enabled(false);
	int64_t time_rt_disabled = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});

	process.set_enabled(true);
	process.callback_once();
	int64_t time_logging = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});
	int64_t time_batched_cb = 0;

	process.callback_every();
	int64_t time_unbatched = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_timing();
		}
	});

	int64_t time_thready = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_no_timing();
		}
	});

	process.callback_once();
	int64_t time_thready_logging = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			fn_thready_timing();
		}
	});

	int64_t time_check_wall = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			check_wall();
		}
	});

	int64_t time_check_cpu = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			check_cpu();
		}
	});

	int64_t time_check_tsc = exec_in_thread([&] {
		for (size_t i = 0; i < TRIALS; ++i) {
			check_tsc();
		}
	});

	int64_t time_unbatched_cbs = time_unbatched - time_logging;

	std::cout
		<< "Trials = " << TRIALS << std::endl
		<< "Payload = " << time_none / TRIALS << "ns" << std::endl
		<< "Overhead when runtime-disabled = " << (time_rt_disabled - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead check wall = " << (time_check_wall - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead check cpu = " << (time_check_cpu - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead check tsc = " << (time_check_tsc - time_none) / TRIALS << "ns per call" << std::endl
		<< "Overhead of timing and storing frame = " << (time_logging - time_none) / TRIALS << "ns per call" << std::endl
		/*
		  I assume a linear model:
		  - time_unbatched_cbs = TRIALS * per_callback_overhead + TRIALS * per_frame_overhead
		  - time_batched_cb = per_callback_overhead + TRIALS * per_frame_overhead
		*/
		<< "Fixed overhead of flush = " << (time_unbatched_cbs - time_batched_cb) / (TRIALS - 1) << "ns" << std::endl
		<< "Variable overhead flush = " << (time_batched_cb - time_unbatched_cbs / TRIALS) / (TRIALS - 1) << "ns per frame" << std::endl
		<< "Thread overhead (due to OS) = " << (time_thready - time_none) / TRIALS << "ns per thread" << std::endl
		<< "Thread overhead (due to scope_timer) = " << (time_thready_logging - time_thready) / TRIALS << "ns" << std::endl
		;

	return 0;
}
