#ifndef _KERNEL_CLOCK_H
#define _KERNEL_CLOCK_H

#include "stdint.h"
#include <kernel/list.h>

enum ClockSourceType
{
	CS_GLOBAL = 1,
	CS_LOCAL = 2,
	CS_RTC = 3,
};

struct clocksource_t
{
	struct list_head list;

	enum ClockSourceType type;

	void (*enable)(struct clocksource_t *);
	void (*disable)(struct clocksource_t *);

	uint64_t (*getFreq)(struct clocksource_t *);
	void (*setFreq)(struct clocksource_t *, uint64_t);

	uint64_t (*val)(struct clocksource_t *);

	void (*countTo)(struct clocksource_t *, uint64_t);
	void (*countNTicks)(struct clocksource_t *, uint64_t);

	void (*enableIRQ)(struct clocksource_t *, uint32_t);
	void (*disableIRQ)(struct clocksource_t *, uint32_t);
};

struct clocksource_t *clock_first(enum ClockSourceType type);

// // Enable the system counter
// void enableSystemCounter();

// // Enable the local counter
// void enableLocalCounter();

// // Get the system counter tick frequency
// uint64_t getSystemCounterFreq();

// // Set the system counter tick frequency
// void setSystemCounterFreq(uint64_t hz);

// // Set the system counter value
// void setSystemCounterValue(uint64_t);

// // Get the system counter value
// uint64_t getSystemCounterValue();

// // Set the system counter compare value
// void setSystemCounterCompareValue(uint64_t);

// // Get the global system counter value
// uint64_t getSysCounterValue();

void RegisterClockSource(struct clocksource_t *clockSource);

void global_clock_init(void);

#endif