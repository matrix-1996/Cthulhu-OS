/*
 * The MIT License (MIT)
 * Copyright (c) 2015 Peter Vanusanik <admin@en-circle.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal in 
 * the Software without restriction, including without limitation the rights to use, copy, 
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
 * and to permit persons to whom the Software is furnished to do so, subject to the 
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies 
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
 * OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN 
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * cpu_mgmt.h
 *  Created on: Dec 23, 2015
 *      Author: Peter Vanusanik
 *  Contents: cpu management
 */

#pragma once

#include "../commons.h"
#include "../memory/paging.h"
#include "../structures/acpi.h"

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <ds/random.h>
#include <ds/array.h>
#include <ds/queue.h>

#include "../processes/process.h"

extern void write_gs(ruint_t addr);

typedef void (*jmp_handler_t)(jmp_buf b, void* fa, ruint_t errno);

typedef struct cpu {
    struct cpu*   self;
    void*         syscall_stack;

    void*    stack;
    void*    handler_stack;
    void*    pf_stack;
    void*    df_stack;
    void*    ipi_stack;

    size_t    insert_id;
    ruint_t   processor_id;
    uint8_t   apic_id;
    uintptr_t current_address_space;

    volatile ruint_t __cpu_lock;
    volatile ruint_t __ipi_lock;
    volatile ruint_t __message_clear_lock;
    volatile ruint_t apic_message;
    volatile ruint_t apic_message2;
    volatile ruint_t apic_message3;
    volatile bool apic_message_handled;
    volatile uint8_t apic_message_type;
    volatile struct cpu* apic_message_cpu;

    /* scheduler info */
    volatile ruint_t __cpu_sched_lock;
    thread_t* ct; // head thread is being executed

    queue_t* priority_0;
    queue_t* priority_1;
    queue_t* priority_2;
    queue_t* priority_3;
    queue_t* priority_4;

    volatile bool started;

    struct {
        jmp_handler_t handler;
        jmp_buf       jmp;
    } pf_handler;
} cpu_t;

#define WAIT_NO_WAIT              (0)
#define WAIT_GENERAL_WAIT         (1)
#define WAIT_SCHEDULER_INIT_WAIT  (2)
#define WAIT_SCHEDULER_QUEUE_CHNG (3)
#define WAIT_KERNEL_MUTEX         (4)

extern array_t* cpus;
extern uint32_t apicaddr;

/**
 * Initializes cpu information. Initializes SMP if available.
 */
void initialize_cpus();

void initialize_mp(unsigned int localcpu);

/**
 * Sends interprocessor interrupt to a processor.
 *
 * Processor is identified by apic_id, vector is data sent,
 * control flags and init_ipi decides flags to be sent with.
 */
void send_ipi_to(uint8_t apic_id, uint8_t vector, uint32_t control_flags, bool init_ipi);

/**
 * Returns pointer to current cpu's cput structure
 */
cpu_t* get_current_cput();

/**
 * Search cpu array by apic predicate
 */
bool search_for_cpu_by_apic(void* e, void* d);

/**
 * Returns local processor_id from MADT, bound local for every cpu
 */
uint8_t get_local_processor_id();

/**
 * Returns local apic_id from MADT, bound local for every cpu
 */
uint8_t get_local_apic_id();

/**
 * Enables IPI interrupts
 */
void enable_ipi_interrupts();

/**
 * Disable IPI interrupts
 */
void disable_ipi_interrupts();

void tlb_shootdown(uintptr_t cr3, uintptr_t from, size_t amount);
void tlb_shootdown_end();

/**
 * Initializes lapic
 */
void initialize_lapic();
