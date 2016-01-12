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
 * task.c
 *  Created on: Dec 27, 2015
 *      Author: Peter Vanusanik
 *  Contents: 
 */

#include "process.h"

#include "../interrupts/clock.h"
#include "../utils/rsod.h"
#include "../memory/paging.h"
#include "scheduler.h"
#include "../loader/elf.h"

#include <stdatomic.h>
#include <errno.h>

ruint_t __proclist_lock;
ruint_t process_id_num;
ruint_t thread_id_num;
array_t* processes;

extern void proc_spinlock_lock(volatile void* memaddr);
extern void proc_spinlock_unlock(volatile void* memaddr);
extern ruint_t __thread_modifier;

pid_t get_current_pid() {
	cpu_t* cpu = get_current_cput();
	proc_spinlock_lock(&cpu->__cpu_lock);
	proc_spinlock_lock(&cpu->__cpu_sched_lock);
	proc_spinlock_lock(&__thread_modifier);

	pid_t pid = cpu->threads == NULL ? -1 : cpu->threads->parent_process->proc_id;

	proc_spinlock_unlock(&__thread_modifier);
	proc_spinlock_unlock(&cpu->__cpu_lock);
	proc_spinlock_unlock(&cpu->__cpu_sched_lock);
	return pid;
}

proc_t* create_init_process_structure(uintptr_t pml) {
    proc_t* process = malloc(sizeof(proc_t));
    if (process == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    proc_spinlock_lock(&__proclist_lock);
    process->proc_id = ++process_id_num;
    array_push_data(processes, process);
    proc_spinlock_unlock(&__proclist_lock);

    process->fds = create_array();
    if (process->fds == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    process->threads = create_array();
    if (process->threads == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    process->pml4 = pml;
    process->mem_maps = NULL;
    process->proc_random = rg_create_random_generator(get_unix_time());
    process->parent = NULL;

    thread_t* main_thread = malloc(sizeof(thread_t));
    if (main_thread == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    memset(main_thread, 0, sizeof(thread_t));
    main_thread->parent_process = process;
    main_thread->tickets = PER_PROCESS_TICKETS;
    main_thread->tId = __atomic_add_fetch(&thread_id_num, 1, __ATOMIC_SEQ_CST);
    main_thread->borrowed_tickets = create_list();
    if (main_thread->borrowed_tickets == NULL) {
        proc_spinlock_unlock(&__proclist_lock);
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    main_thread->lended_tickets = create_list();
    if (main_thread->lended_tickets == NULL) {
        proc_spinlock_unlock(&__proclist_lock);
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    main_thread->last_rdi = (ruint_t)(uintptr_t)process->argc;
    main_thread->last_rsi = (ruint_t)(uintptr_t)process->argv;
    main_thread->last_rdx = (ruint_t)(uintptr_t)process->environ;
    array_push_data(process->threads, main_thread);

    return process;
}

int fork_process(registers_t* r, proc_t* p, thread_t* t) {
    proc_t* process = malloc(sizeof(proc_t));
    if (process == NULL) {
        return ENOMEM;
    }

    process->fds = create_array();
    if (process->fds == NULL) {
        return ENOMEM;
    }
    for (uint32_t i=0; i<array_get_size(p->fds); i++) {
        fd_t* fe = (fd_t*)array_get_at(p->fds, i);
        fd_t* nfe = malloc(sizeof(fd_t));
        if (nfe == NULL) {
            for (uint32_t i=0; i<array_get_size(p->fds); i++)
                free(array_get_at(p->fds, i));
            destroy_array(process->fds);
            free(process);
            return ENOMEM;
        }
        memcpy(nfe, fe, sizeof(fd_t));
        array_push_data(p->fds, nfe);
    }
    process->threads = create_array();
    if (process->threads == NULL) {
        for (uint32_t i=0; i<array_get_size(p->fds); i++)
            free(array_get_at(p->fds, i));
        destroy_array(process->fds);
        free(process);
        return ENOMEM;
    }
    process->pml4 = clone_paging_structures();
    process->mem_maps = NULL;
    process->proc_random = rg_create_random_generator(get_unix_time());
    process->parent = p;

    thread_t* main_thread = malloc(sizeof(thread_t));
    if (main_thread == NULL) {
        destroy_array(process->threads);
        for (uint32_t i=0; i<array_get_size(p->fds); i++)
            free(array_get_at(p->fds, i));
        destroy_array(process->fds);
        free(process);
        return ENOMEM;
    }
    memset(main_thread, 0, sizeof(thread_t));
    main_thread->parent_process = process;
    main_thread->tickets = PER_PROCESS_TICKETS;
    main_thread->tId = __atomic_add_fetch(&thread_id_num, 1, __ATOMIC_SEQ_CST);
    main_thread->next_thread = NULL;
    main_thread->prev_thread = NULL;
    main_thread->borrowed_tickets = create_list();
    if (main_thread->borrowed_tickets == NULL) {
        free(main_thread);
        destroy_array(process->threads);
        for (uint32_t i=0; i<array_get_size(p->fds); i++)
            free(array_get_at(p->fds, i));
        destroy_array(process->fds);
        free(process);
        proc_spinlock_unlock(&__proclist_lock);
        return ENOMEM;
    }
    main_thread->lended_tickets = create_list();
    if (main_thread->lended_tickets == NULL) {
        free_list(main_thread->borrowed_tickets);
        free(main_thread);
        destroy_array(process->threads);
        for (uint32_t i=0; i<array_get_size(p->fds); i++)
            free(array_get_at(p->fds, i));
        destroy_array(process->fds);
        free(process);
        proc_spinlock_unlock(&__proclist_lock);
        return ENOMEM;
    }
    array_push_data(process->threads, main_thread);

    proc_spinlock_lock(&__proclist_lock);
    process->proc_id = ++process_id_num;
    array_push_data(processes, process);
    mmap_area_t* mmap = p->mem_maps;
	while (mmap != NULL) {
		++mmap->count;
		mmap = mmap->next;
	}
	process->mem_maps = p->mem_maps;
    proc_spinlock_unlock(&__proclist_lock);

    copy_registers(r, main_thread);

    // TODO: add split option?
    main_thread->last_rax = process->proc_id;
    enschedule_best(main_thread);
    return 0;
}

borrowed_ticket_t* transfer_tickets(thread_t* from, thread_t* to, uint16_t tamount) {
    if (tamount > from->tickets)
        tamount = from->tickets;
    if (tamount == 0)
        return NULL;

    borrowed_ticket_t* bt = malloc(sizeof(borrowed_ticket_t));
    bt->source = from;
    bt->target = to;
    bt->tamount = tamount;
    bt->release_now = false;

    list_push_right(from->lended_tickets, bt);
    list_push_right(to->borrowed_tickets, bt);

    from->tickets -= tamount;
    to->tickets += tamount;

    return bt;
}

void initialize_processes() {
    __proclist_lock = 0;
    process_id_num = 0;
    thread_id_num = 0;
    processes = create_array_spec(256);
}

mmap_area_t* request_va_hole(proc_t* proc, uintptr_t start_address, size_t req_size) {
    mmap_area_t** lm = &proc->mem_maps;
    uintptr_t x1, x2, y1, y2;

    while (*lm != NULL) {
        mmap_area_t* mmap = *lm;

        x1 = start_address;
        x2 = start_address + req_size;
        y1 = mmap->vastart;
        y2 = mmap->vaend;

        if (start_address >= mmap->vaend) {
            lm = &mmap->next;
            continue;
        } else if (x1 <= y2 && y1 <= x2) {
            // section overlaps
            return NULL;
        } else {
            // we have found the section above, therefore we are done with the search and we need to
            // insert
            break;
        }
    }

    mmap_area_t* newmm = malloc(sizeof(mmap_area_t));
    memset(newmm, 0, sizeof(mmap_area_t));
    newmm->next = *lm;
    newmm->vastart = start_address;
    newmm->vaend = start_address + req_size;
    newmm->count = 1;
    *lm = newmm;
    return newmm;
}

#define ALIGN_DOWN(a, b) ((a) - (a % b))
#define ALIGN_UP(a, b) ((a % b == 0) ? (a) : (ALIGN_DOWN(a, b) + b))

mmap_area_t* find_va_hole(proc_t* proc, size_t req_size, size_t align_amount) {
    mmap_area_t** lm = &proc->mem_maps;
    uintptr_t start_address = 0;
    uintptr_t hole = 0;

    if (*lm == NULL)
        hole = 0x800000000000;

    while (*lm != NULL) {
        mmap_area_t* mmap = *lm;

        hole = mmap->vastart - start_address;
        if (hole >= req_size+align_amount)
            break; // hole found

        start_address = mmap->vaend;
        lm = &mmap->next;
        if (*lm == NULL) {
            hole = 0x800000000000 - start_address;
        }
    }

    start_address = ALIGN_UP(start_address, align_amount);
    size_t diff_holes = hole - req_size;
    uintptr_t offset;
    if (diff_holes == 0)
        offset = 0;
    else
        offset = rg_next_uint_l(&proc->proc_random, diff_holes);
    offset = ALIGN_DOWN(offset, align_amount);
    mmap_area_t* newmm = malloc(sizeof(mmap_area_t));
    memset(newmm, 0, sizeof(mmap_area_t));
    newmm->next = *lm;
    newmm->vastart = start_address + offset;
    newmm->vaend = start_address + req_size + offset;
    newmm->count = 1;
    *lm = newmm;
    return newmm;
}

uintptr_t map_virtual_virtual(uintptr_t vastart, uintptr_t vaend, bool readonly) {
	uintptr_t vaoffset = vastart % 0x1000;
	vastart = ALIGN_DOWN(vastart, 0x1000);
	vaend = ALIGN_UP(vaend, 0x1000);

	cpu_t* cpu = get_current_cput();
	proc_spinlock_lock(&cpu->__cpu_lock);
	proc_spinlock_lock(&cpu->__cpu_sched_lock);
	proc_spinlock_lock(&__thread_modifier);

	proc_t* proc = cpu->threads->parent_process;

	proc_spinlock_unlock(&__thread_modifier);
	proc_spinlock_unlock(&cpu->__cpu_lock);
	proc_spinlock_unlock(&cpu->__cpu_sched_lock);

	mmap_area_t* hole = find_va_hole(proc, vaend-vastart, 0x1000);
	hole->mtype = kernel_allocated_heap_data;
	map_range(vastart, vaend, hole->vastart, hole->vaend, true, readonly, false);
	return hole->vastart+vaoffset;
}

uintptr_t map_physical_virtual(puint_t vastart, puint_t vaend, bool readonly) {
	uintptr_t vaoffset = vastart % 0x1000;
	vastart = ALIGN_DOWN(vastart, 0x1000);
	vaend = ALIGN_UP(vaend, 0x1000);

	cpu_t* cpu = get_current_cput();
	proc_spinlock_lock(&cpu->__cpu_lock);
	proc_spinlock_lock(&cpu->__cpu_sched_lock);
	proc_spinlock_lock(&__thread_modifier);

	proc_t* proc = cpu->threads->parent_process;

	proc_spinlock_unlock(&__thread_modifier);
	proc_spinlock_unlock(&cpu->__cpu_lock);
	proc_spinlock_unlock(&cpu->__cpu_sched_lock);

	mmap_area_t* hole = find_va_hole(proc, vaend-vastart, 0x1000);
	hole->mtype = nondealloc_map;
	map_range(vastart, vaend, hole->vastart, hole->vaend, false, readonly, false);
	return hole->vastart+vaoffset;
}

void* proc_alloc(size_t size) {
	cpu_t* cpu = get_current_cput();
	proc_spinlock_lock(&cpu->__cpu_lock);
	proc_spinlock_lock(&cpu->__cpu_sched_lock);
	proc_spinlock_lock(&__thread_modifier);

	proc_t* proc = cpu->threads->parent_process;
	void* addr = proc_alloc_direct(proc, size);

	proc_spinlock_unlock(&__thread_modifier);
	proc_spinlock_unlock(&cpu->__cpu_lock);
	proc_spinlock_unlock(&cpu->__cpu_sched_lock);
	return addr;
}

void* proc_alloc_direct(proc_t* proc, size_t size) {
	mmap_area_t* hole = find_va_hole(proc, size, 16);
	hole->mtype = kernel_allocated_heap_data;
	allocate(hole->vastart, size, false, false);
	return (void*)hole->vastart;
}

static int cpy_array(int count, char*** a) {
	char** array = *a;
	char** na = malloc(8*(count+1));
	if (na == NULL)
		return ENOMEM;

	int i = 0;
	for (; i<count; i++) {
		char* cpy = malloc(strlen(array[i])+1);
		if (cpy == NULL) {
			for (int j=0; j<i; j++) {
				free(na[j]);
			}
			free(na);
			return ENOMEM;
		}
		memcpy(cpy, array[i], strlen(array[i])+1);
		na[i] = cpy;
	}
	na[count] = NULL;
	*a = na;
	return 0;
}

static int cpy_array_user(int count, char*** a, proc_t* p) {
	char** array = *a;
	char** na = proc_alloc_direct(p, 8*(count+1));
	if (na == NULL)
		return ENOMEM;

	int i = 0;
	for (; i<count; i++) {
		char* cpy = proc_alloc_direct(p, strlen(array[i])+1);
		if (cpy == NULL) {
			return ENOMEM;
		}
		memcpy(cpy, array[i], strlen(array[i])+1);
		na[i] = cpy;
	}
	na[count] = NULL;
	*a = na;
	return 0;
}

void free_proc_memory(proc_t* proc) {
	mmap_area_t* mm = proc->mem_maps;
	while (mm != NULL) {
		uint64_t use_count = __atomic_sub_fetch(&mm->count, 1, __ATOMIC_SEQ_CST);
		switch (mm->mtype) {
		case program_data:
		case stack_data:
		case heap_data:
		case kernel_allocated_heap_data: {
			deallocate(mm->vastart, mm->vaend-mm->vastart);
		} break;
		case nondealloc_map:
			break;
		}
		mmap_area_t* mmn = mm->next;
		if (use_count == 0) {
			free(mm);
		}
		mm = mmn;
	}
}

static void free_array(int count, char** a) {
	for (int i=0; i<count; i++) {
		free(a[i]);
	}
	free(a);
}

int sys_execve(uint8_t* image_data, int argc, char** argv, char** envp, registers_t* r) {
	int envc = 0;
	char** envt = envp;
	while (*envt != NULL) {
		++envc;
		++envt;
	}

	int err;
	if ((err = cpy_array(argc, &argv)) != 0)
		return err;
	if ((err = cpy_array(envc, &envp)) != 0) {
		free_array(argc, argv);
		return err;
	}
	// envp and argv are now kernel structures

	cpu_t* cpu = get_current_cput();
	proc_spinlock_lock(&cpu->__cpu_lock);
	proc_spinlock_lock(&cpu->__cpu_sched_lock);
	proc_spinlock_lock(&__thread_modifier);

	proc_t* p = cpu->threads->parent_process;
	thread_t* main_thread = cpu->threads;
	free_proc_memory(p);
	p->mem_maps = NULL;

	for (uint32_t tid=0; tid<array_get_size(p->threads); tid++) {
		thread_t* t = array_get_at(p->threads, tid);
		if (t != main_thread) {
			// TODO: add freeing threads
			// TODO: add ticket merging
			free(t);
		}
	}

	array_clean(p->threads);
	array_push_data(p->threads, main_thread);

	err = load_elf_exec((uintptr_t)image_data, p);
	if (err == ELF_ERROR_ENOMEM) {
		err = ENOMEM;
	} else if (err != 0) {
		err = EINVAL;
	}

	if (err != 0) {
		// TODO: add abort
		proc_spinlock_unlock(&__thread_modifier);
		proc_spinlock_unlock(&cpu->__cpu_lock);
		proc_spinlock_unlock(&cpu->__cpu_sched_lock);
		return err;
	}

	char** argvu = argv;
	char** envpu = envp;
	if ((err = cpy_array_user(argc, &argvu, p)) != 0) {
		// TODO: add abort
		proc_spinlock_unlock(&__thread_modifier);
		proc_spinlock_unlock(&cpu->__cpu_lock);
		proc_spinlock_unlock(&cpu->__cpu_sched_lock);
		return err;
	}
	if ((err = cpy_array_user(envc, &envpu, p)) != 0) {
		// TODO: add abort
		proc_spinlock_unlock(&__thread_modifier);
		proc_spinlock_unlock(&cpu->__cpu_lock);
		proc_spinlock_unlock(&cpu->__cpu_sched_lock);
		return err;
	}

	p->argc = argc;
	p->argv = argvu;
	p->environ = envp;

	main_thread->last_r12 = 0;
	main_thread->last_r11 = 0;
	main_thread->last_r10 = 0;
	main_thread->last_r9 = 0;
	main_thread->last_r8 = 0;
	main_thread->last_rax = 0;
	main_thread->last_rbx = 0;
	main_thread->last_rcx = 0;
	main_thread->last_rdx = 0;
	main_thread->last_rdi = 0;
	main_thread->last_rsi = 0;
	main_thread->last_rbp = 0;

	cpu->threads = main_thread;

	// TODO: do check tickets etc

	// TODO: remove up when do proper cleanup

	main_thread->tickets = PER_PROCESS_TICKETS;
	free_list(main_thread->borrowed_tickets);
	free_list(main_thread->lended_tickets);

	main_thread->borrowed_tickets = create_list();
	main_thread->lended_tickets = create_list();

	main_thread->last_rdi = (ruint_t)(uintptr_t)p->argc;
	main_thread->last_rsi = (ruint_t)(uintptr_t)p->argv;
	main_thread->last_rdx = (ruint_t)(uintptr_t)p->environ;

	registers_copy(main_thread, r);

	free_array(argc, argv);
	free_array(envc, envp);

	proc_spinlock_unlock(&__thread_modifier);
	proc_spinlock_unlock(&cpu->__cpu_lock);
	proc_spinlock_unlock(&cpu->__cpu_sched_lock);

	return 0;
}
