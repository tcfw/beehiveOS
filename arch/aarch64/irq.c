#include "errno.h"
#include "gic.h"
#include "regs.h"
#include "regs.h"
#include <kernel/arch.h>
#include <kernel/cls.h>
#include <kernel/debug.h>
#include <kernel/irq.h>
#include <kernel/panic.h>
#include <kernel/stdint.h>
#include <kernel/syscall.h>
#include <kernel/thread.h>
#include <kernel/tty.h>
#include <kernel/umm.h>
#include <kernel/vm.h>

extern unsigned long stack;

#define KSTACKEXC stack - 0x15000
#define KSTACKSWI stack - 0x05000

#define ARM4_XRQ_SYNC 0x01
#define ARM4_XRQ_IRQ 0x02
#define ARM4_XRQ_FIQ 0x03
#define ARM4_XRQ_SERROR 0x04

#define PIC_IRQ_STATUS 0x0
#define PIC_IRQ_RAWSTAT 0x1
#define PIC_IRQ_ENABLESET 0x2
#define PIC_IRQ_ENABLECLR 0x3
#define PIC_INT_SOFTSET 0x4
#define PIC_INT_SOFTCLR 0x5

#define SOFT_IRQ_MAX 10

#define KEXP_TOP3                                             \
	uint64_t spsr = 0;                                        \
	__asm__ volatile("mrs %0, SPSR_EL1"                       \
					 : "=r"(spsr));                           \
	thread_t *thread = current;                               \
	int didsave = 0;                                          \
	int iskthread = 0;                                        \
	if (thread != 0)                                          \
		iskthread = thread->flags & THREAD_KTHREAD;           \
	if ((spsr & SPSR_M_MASK) == SPSR_M_EL0 || iskthread != 0) \
	{                                                         \
		didsave = 1;                                          \
		save_to_context(&thread->ctx, trapFrame);             \
	}

#define KEXP_BOT3                                                         \
	thread_t *next = current;                                             \
	if (next != thread && didsave)                                        \
	{                                                                     \
		thread = next;                                                    \
		vm_set_table(thread->process->vm.vm_table, thread->process->pid); \
	}                                                                     \
	set_to_context(&thread->ctx, trapFrame);

// handle syscall inner
void k_exphandler_swi_entry(uintptr_t trapFrame)
{
	KEXP_TOP3

	// check svc number
	uint8_t int_vector = 0;
	__asm__ volatile("MRS %0, ESR_EL1"
					 : "=r"(int_vector));
	int_vector &= 0xffffff;

	if (int_vector != 0)
	{
		int ret = -ERRNOSYS;
		__asm__ volatile("MOV x0, %0" ::"r"(ret)
						 : "x0");
		return;
	}

	set_cls_irq_cause(INTERRUPT_CAUSE_SWI);

	uint64_t x0, x1, x2, x3, x4, x5;
	x0 = thread->ctx.regs[0];
	x1 = thread->ctx.regs[1];
	x2 = thread->ctx.regs[2];
	x3 = thread->ctx.regs[3];
	x4 = thread->ctx.regs[4];
	x5 = thread->ctx.regs[5];

	uint64_t ret = ksyscall_entry(x0, x1, x2, x3, x4, x5);
	if (current == thread)
	{
		thread->ctx.regs[0] = ret;

		// set carry flag to denote an error
		if ((int64_t)ret < 0)
			thread->ctx.spsr |= 1 << 29;
		else
			thread->ctx.spsr &= ~(1 << 29);
	}

	clear_cls_irq_cause();

	uint64_t *pending_irq = &get_cls()->pending_irq;
	if (*pending_irq)
	{
		set_cls_irq_cause(INTERRUPT_CAUSE_PENDING_IRQ);

		k_deferred_exphandler(ARM4_XRQ_IRQ, *pending_irq);
		*pending_irq = 0;

		clear_cls_irq_cause();
	}

	// clear ESR
	__asm__ volatile("MOV x0, #0; MSR ESR_EL1, x0");

	KEXP_BOT3
}

// Handle non-syscall sync exceptions
void k_exphandler_sync(uintptr_t trapFrame)
{
	KEXP_TOP3;

	uint64_t esr = 0;
	__asm__ volatile("mrs %0, ESR_EL1"
					 : "=r"(esr));

	k_exphandler(ARM4_XRQ_SYNC, esr, 0);

	KEXP_BOT3;
}

// Handle IRQ
void k_exphandler_irq(uintptr_t trapFrame)
{
	KEXP_TOP3;

	volatile uint64_t xrq;
	__asm__ volatile("MRS %0, S3_0_c12_c12_0" // ICC_IAR1_EL1
					 : "=r"(xrq));

	volatile uint64_t esr;
	__asm__ volatile("MRS %0, ESR_EL1"
					 : "=r"(esr));

	if (xrq < SOFT_IRQ_MAX)
	{
		// Handle IPI immediately
		k_exphandler(ARM4_XRQ_IRQ, xrq, 0);
	}

	if (get_cls_irq_cause() == INTERRUPT_CAUSE_SWI)
	{
		// defer IRQ until syscall is complete
		get_cls()->pending_irq |= (1 << xrq);
	}
	else
	{
		set_cls_irq_cause(INTERRUPT_CAUSE_IRQ);

		k_exphandler(ARM4_XRQ_IRQ, xrq, 0);

		clear_cls_irq_cause();
	}

	enable_xrq();

	KEXP_BOT3;
}

// Handle FIQ
void k_exphandler_fiq(uintptr_t trapFrame)
{
	KEXP_TOP3;

	k_exphandler(ARM4_XRQ_FIQ, 0, 0);

	enable_xrq();

	KEXP_BOT3;
}

// Handle serrors
void k_exphandler_serror_entry(uintptr_t trapFrame)
{
	KEXP_TOP3;

	k_exphandler(ARM4_XRQ_SERROR, 0, 0);

	KEXP_BOT3;
}

// Acknowledge the group 1 interrupt
void ack_xrq(int xrq)
{
	gic_cpu_eoi_gp1(xrq);
}

// disable IRQ & FIQ interrupts
void disable_xrq()
{
	gic_cpu_disable();
}

// enable IRQ & FIQ interrupts & reset priority mask
void enable_xrq()
{
	gic_cpu_enable();
	gic_cpu_set_priority_mask(0xFF);
}

// Disable interrutps for the local core
// for when handling syscalls
void disable_irq(void)
{
	// Disable all DAIF
	__asm__ volatile("MOV x0, #0x3C0; MSR DAIF, X0; ISB");
}

// Enable interrupts for the local core
// for when handling syscalls
void enable_irq(void)
{
	// Enable all DAIF
	__asm__ volatile("ISB; MOV x0, #0; MSR DAIF, X0; ISB");
}

// enable the specific interrupt number and route
// to the current PE
// all interrupts map to group 1 NS & level config
void enable_xrq_n(unsigned int xrq)
{
	enable_xrq_n_prio(xrq, 0x10);
}

void enable_xrq_n_prio(unsigned int xrq, uint8_t prio)
{
	uint32_t affinity = cpu_id();
	uint32_t rd = getRedistID(affinity);

	gic_redist_set_int_priority(xrq, rd, prio);
	gic_redist_set_int_group(xrq, rd, GICV3_GROUP1_NON_SECURE);
	gic_redist_enable_int(xrq, rd);
	gic_dist_enable_xrq_n(affinity, xrq);
	gic_dist_xrq_config(xrq, GICV3_CONFIG_LEVEL);
	gic_dist_target(xrq, GICV3_ROUTE_MODE_ANY, affinity);
}

void xrq_set_priority(unsigned int xrq, uint8_t prio)
{
	uint32_t affinity = cpu_id();
	uint32_t rd = getRedistID(affinity);

	gic_redist_set_int_priority(xrq, rd, prio);
}

uint8_t xrq_get_max_priority()
{
	uint32_t affinity = cpu_id();
	uint32_t rd = getRedistID(affinity);

	uint32_t xrq = 27;
	uint8_t initxrqprio = gic_redist_get_int_priority(xrq, rd);
	gic_redist_set_int_priority(xrq, rd, 0xFF);
	arch_mb;
	uint8_t max = gic_redist_get_int_priority(xrq, rd);
	gic_redist_set_int_priority(xrq, rd, initxrqprio);

	return max;
}

void xrq_set_trigger_type(unsigned int xrq, uint8_t type)
{
	if (type == 1)
	{
		gic_dist_xrq_config(xrq, GICV3_CONFIG_EDGE);
		return;
	}

	gic_dist_xrq_config(xrq, GICV3_CONFIG_LEVEL);
}

// Send a software generated IRQ to all targets, except self
void send_soft_irq_all_cores(uint8_t sgi)
{
	volatile uint64_t icc_sgi = ((uint64_t)sgi & 0x04) << 24;
	icc_sgi |= (1ULL << 40); // IRM

	__asm__ volatile("MSR S3_0_c12_c11_5, %0" ::"r"(icc_sgi)); // ICC_SGI1R_EL1
}

// Send a software generated IRQ to a specific targets
void send_soft_irq(uint64_t target, uint8_t sgi)
{
	uint8_t aff3 = 0, aff2 = 0, aff1 = 0;
	uint16_t aff_target = 0;

	volatile uint64_t icc_sgi = (uint64_t)(sgi & 0x4) << 24;
	__asm__ volatile("MSR S3_0_c12_c11_5, %0" ::"r"(icc_sgi)); // ICC_SGI1R_EL1
}

// Handle FIQ exceptions
void k_fiq_exphandler(unsigned int xrq)
{
	panicf("unhandled FIQ xrq=0x%x", xrq);
}

// Handle SYNC exceptions
void k_sync_exphandler(unsigned int xrq)
{
	uint64_t far = 0, par = 0, pa = 0, elr = 0;
	__asm__ volatile("MRS %0, S3_0_c6_c0_0"
					 : "=r"(far)); // FAR_EL1
	__asm__ volatile("MRS %0, S3_0_c7_c4_0"
					 : "=r"(par)); // PAR_EL1
	__asm__ volatile("MRS %0, ELR_EL1"
					 : "=r"(elr)); // ELR_EL1

	cls_t *cls = get_cls();

	switch (ESR_EXCEPTION_CLASS(xrq))
	{
	case ESR_EXCEPTION_INSTRUCTION_ABORT_LOWER_EL:
		terminal_logf("instruction abort from EL0 addr 0x%X (reason: 0x%X)", far, xrq);
		if (cls->rq.current_thread != 0)
		{
			terminal_logf("on PID 0x%x", cls->rq.current_thread->process->pid);
			// send SIGILL
			current->state = THREAD_DEAD;
		}
		break;
	case ESR_EXCEPTION_INSTRUCTION_ABORT_SAME_EL:
		if (cls->cfe != EXCEPTION_UNKNOWN)
		{
			if (cls->cfe_handle != 0)
				cls->cfe_handle();
			// send SIGSYS?
		}
		else
			panicf("Unhandled instruction abort from kernel: at 0x%X", far);
	case ESR_EXCEPTION_DATA_ABORT_LOWER_EL:
		int wnr = USER_DATA_ABORT_READ;
		if (ESR_WNR(xrq) == 1)
			wnr = USER_DATA_ABORT_WRITE;

		user_data_abort(far, wnr, elr);

		return;

		// uint64_t *pte = vm_va_to_pte(current->process->vm.vm_table, (uintptr_t)far);
		// terminal_logf("data abort from EL0 addr accessing 0x%X\r\nPC:0x%X\r\nESR:0x%X\r\nPage: *0x%X", far, elr, xrq, pte);
		// if (cls->rq.current_thread != 0)
		// {
		// 	terminal_logf("on PID 0x%x", cls->rq.current_thread->process->pid);
		// 	// send SIGSEGV
		// 	current->state = THREAD_DEAD;
		// }

		// break;
	case ESR_EXCEPTION_DATA_ABORT_SAME_EL:
		if (cls->cfe != EXCEPTION_UNKNOWN)
		{
			if (cls->cfe_handle != 0)
				cls->cfe_handle();

			// send SIGSEGV
		}
		else
		{
			pa = vm_va_to_pa(vm_get_current_table(), far);
			panicf("Unhandlable data abort from kernel: \r\n\tELR: 0x%X \r\n\tESR: 0x%x \r\n\tVirtual Address: 0x%X\r\n\tPhysical Address: 0x%X\r\n\tPAR: 0x%X", elr, xrq, far, pa, par);
		}
		break;
	case ESR_EXCEPTION_SOFTWARE_STEP_LOWER_EL:
		user_debug_software_step_handler(cls->rq.current_thread);
		break;
	case ESR_EXCEPTION_SOFTWARE_STEP_SAME_EL:
	case ESR_EXCEPTION_WATCHPOINT_LOWER_EL:
	case ESR_EXCEPTION_WATCHPOINT_SAME_EL:
	default:
		current->state = THREAD_DEAD;
		panicf("unhandled SYNC TID=%d:%d xrq=0x%x FAR=0x%X PAR=0x%X ELR=0x%X", current->process->pid, current->tid, xrq, far, par, elr);
	}
}

// Init the vector table and GICv3 distributor and redistribute
// for the current PE
void init_xrq(void)
{
	// Load EL1 vectors
	__asm__ volatile("LDR x0, =vectors");
	__asm__ volatile("MSR VBAR_EL1, x0");

	uint32_t affinity = cpu_id();

	if (affinity == 0)
	{
		setGICAddr((void *)GIC_DIST_BASE, (void *)GIC_REDIST_BASE, (void *)GIC_CPU_BASE);
		gic_dist_enable();
	}

	uint32_t rd = getRedistID(affinity);
	gic_redist_enable(rd);

	gic_cpu_init();
}