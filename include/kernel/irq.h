#ifndef _KERNEL_IRQ_H
#define _KERNEL_IRQ_H

#include <kernel/thread.h>
#include <kernel/stdint.h>

#define IRQ_FIQ (0x2)

#define SOFT_IRQ_HALT_CORE (0)
#define SOFT_IRQ_THREAD_STOP (1)

// Init the interrupt hardware for all cores
void init_xrq(void);

// Disable interrutps for the local core
void disable_xrq(void);

// Enable interrupts for the local core
void enable_xrq(void);

// Disable interrutps for the local core
// for when handling syscalls
void disable_irq(void);

// Enable interrupts for the local core
// for when handling syscalls
void enable_irq(void);

// Enable the specific interrupt for the local core
void enable_xrq_n(unsigned int);

// Specify a priority for a specific xrq on the local core
void xrq_set_priority(unsigned int xrq, uint8_t prio);

uint8_t xrq_get_max_priority();

void enable_xrq_n_prio(unsigned int xrq, uint8_t prio);

// Specify a trigger type for the specific xrq on the local core
//  1=EDGE, 0=LEVEL(default)
void xrq_set_trigger_type(unsigned int xrq, uint8_t type);

// Acknowledge the current interrupt
void ack_xrq(int);

// Acknowledge all current and pending interrupts
void ack_all_xrq(void);

// Handles the specific interrupt given it's type (e.g. IRQ, FIQ, etc.) and
// number identifier
void k_exphandler(unsigned int type, unsigned int xrq, int deferred);

// Handles sync interrupts
void k_sync_exphandler(unsigned int xrq);

// Handles FIQ interrupts
void k_fiq_exphandler(unsigned int xrq);

// Initiate software interrupts
void k_setup_soft_irq();

// Initiate clock interrupts
void k_setup_clock_irq();

// handle deferred xrqs
void k_deferred_exphandler(unsigned int type, uint64_t xrq_map);

// Interrupt handler
typedef void (*irq_handler_cb)(unsigned int type);

struct irq_handler_t
{
	irq_handler_cb khandler;
	pid_t pid;
};

// Assign an interrupt number to a handler
void assign_irq_hook(unsigned int xrq, irq_handler_cb cb);

// Send a software interrupt to all other cores
void send_soft_irq_all_cores(uint8_t sgi);

// Send a software interrupt to a specific core
void send_soft_irq(uint64_t target, uint8_t sgi);

#endif