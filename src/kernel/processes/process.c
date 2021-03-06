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
#include "../syscalls/sys.h"

#include <stdatomic.h>
#include <errno.h>
#include <ds/hmap.h>

enum proc_state {
	CP1,
};

typedef struct temp_proc {
	enum proc_state cstate;
	proc_t* process;
} temp_proc_t;

ruint_t __proclist_lock;
ruint_t __proclist_lock2;
ruint_t process_id_num;
ruint_t thread_id_num;
list_t* processes;
hash_table_t* temp_processes;

extern void proc_spinlock_lock(volatile void* memaddr);
extern void proc_spinlock_unlock(volatile void* memaddr);
extern void set_active_page(void* address);
extern void* get_active_page();
extern ruint_t __thread_modifier;

pid_t get_current_pid() {
    cpu_t* cpu = get_current_cput();
    proc_spinlock_lock(&cpu->__cpu_lock);
    proc_spinlock_lock(&cpu->__cpu_sched_lock);
    proc_spinlock_lock(&__thread_modifier);

    pid_t pid = cpu->ct == NULL ? -1 : cpu->ct->parent_process->proc_id;

    proc_spinlock_unlock(&__thread_modifier);
    proc_spinlock_unlock(&cpu->__cpu_lock);
    proc_spinlock_unlock(&cpu->__cpu_sched_lock);
    return pid;
}

proc_t* get_current_process() {
    cpu_t* cpu = get_current_cput();
    proc_spinlock_lock(&cpu->__cpu_lock);
    proc_spinlock_lock(&cpu->__cpu_sched_lock);
    proc_spinlock_lock(&__thread_modifier);

    proc_t* proc = cpu->ct->parent_process;

    proc_spinlock_unlock(&__thread_modifier);
    proc_spinlock_unlock(&cpu->__cpu_lock);
    proc_spinlock_unlock(&cpu->__cpu_sched_lock);
    return proc;
}

static struct chained_element* __blocked_getter(void* data) {
    return &(((thread_t*)data)->blocked_list);
}

static struct chained_element* __message_getter(void* data) {
    return &(((_message_t*)data)->target_list);
}

static struct chained_element* __process_get_function(void* data) {
    return &((proc_t*)data)->process_list;
}

proc_t* create_init_process_structure(uintptr_t pml) {
    proc_t* process = malloc(sizeof(proc_t));
    if (process == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    memset(process, 0, sizeof(proc_t));

    proc_spinlock_lock(&__proclist_lock);
    process->proc_id = ++process_id_num;
    list_push_right(processes, process);
    proc_spinlock_unlock(&__proclist_lock);

    process->fds = create_array();
    process->pprocess = true;
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
    process->priority = 0;
    process->process_list.data = process;
    process->futexes = create_uint64_table();
    process->__ob_lock = 0;
    if (process->futexes == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    process->input_buffer = create_queue_static(__message_getter);
    if (process->input_buffer == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    process->pq_input_buffer = create_queue_static(__message_getter);
    if (process->pq_input_buffer == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    process->blocked_wait_messages = create_list_static(__message_getter);
    if (process->blocked_wait_messages == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    process->temp_processes = create_list_static(__process_get_function);
	if (process->blocked_wait_messages == NULL) {
		error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
	}

    thread_t* main_thread = malloc(sizeof(thread_t));
    if (main_thread == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    memset(main_thread, 0, sizeof(thread_t));
    main_thread->parent_process = process;
    main_thread->tId = __atomic_add_fetch(&thread_id_num, 1, __ATOMIC_SEQ_CST);
    main_thread->priority = 0;
    main_thread->futex_block = create_list_static(__blocked_getter);
    if (main_thread->futex_block == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    main_thread->blocked = false;
    main_thread->blocked_list.data = main_thread;
    main_thread->schedule_list.data = main_thread;
    main_thread->last_rdi = (ruint_t)(uintptr_t)process->argc;
    main_thread->last_rsi = (ruint_t)(uintptr_t)process->argv;
    main_thread->last_rdx = (ruint_t)(uintptr_t)process->environ;

    main_thread->continuation = malloc(sizeof(continuation_t));
    if (main_thread->continuation == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    main_thread->continuation->present = false;

    if (array_push_data(process->threads, main_thread) == 0) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }

    return process;
}

void process_init(proc_t* process) {
    thread_t* main_thread = array_get_at(process->threads, 0);
    main_thread->local_info = proc_alloc_direct(process, sizeof(tli_t));
    if (main_thread->local_info == NULL) {
        error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
    }
    main_thread->local_info->self = main_thread->local_info;
    main_thread->local_info->t = main_thread->tId;


    for (int i=0; i<MESSAGE_BUFFER_CNT; i++) {
        _message_t* m = &process->output_buffer[i];
        m->owner = process;
        m->used = false;
        m->message = proc_alloc_direct(process, 0x200000);
        if (m->message == NULL) {
            error(ERROR_MINIMAL_MEMORY_FAILURE, 0, 0, &create_init_process_structure);
        }
    }
}

void initialize_processes() {
    __proclist_lock = 0;
    process_id_num = 0;
    thread_id_num = 0;
    __proclist_lock2 = 0;
    processes = create_list_static(__process_get_function);
    temp_processes = create_uint64_table();
}

mmap_area_t** mmap_area(proc_t* proc, uintptr_t address) {
    mmap_area_t** lm = &proc->mem_maps;
    uintptr_t x1, x2, y1, y2;

    while (*lm != NULL) {
        mmap_area_t* mmap = *lm;

        x1 = address;
        x2 = address;
        y1 = mmap->vastart;
        y2 = mmap->vaend;

        if (address >= mmap->vaend) {
            lm = &mmap->next;
            continue;
        } else if (x1 <= y2 && y1 <= x2) {
            // section overlaps
            return lm;
        } else {
            break;
        }
    }

    return NULL;
}

mmap_area_t** request_va_hole(proc_t* proc, uintptr_t start_address, size_t req_size) {
    mmap_area_t** lm = &proc->mem_maps;
    uintptr_t x1, x2, y1, y2;

    if (start_address < 0x1000)
        return NULL;

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
    return lm;
}

mmap_area_t** find_va_hole(proc_t* proc, size_t req_size, size_t align_amount) {
    mmap_area_t** lm = &proc->mem_maps;
    uintptr_t start_address = 0x1000;
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
    return lm;
}

void* proc_alloc(size_t size) {
    cpu_t* cpu = get_current_cput();
    proc_spinlock_lock(&cpu->__cpu_lock);
    proc_spinlock_lock(&cpu->__cpu_sched_lock);
    proc_spinlock_lock(&__thread_modifier);

    proc_t* proc = cpu->ct->parent_process;
    void* addr = proc_alloc_direct(proc, size);

    proc_spinlock_unlock(&__thread_modifier);
    proc_spinlock_unlock(&cpu->__cpu_lock);
    proc_spinlock_unlock(&cpu->__cpu_sched_lock);
    return addr;
}

void* proc_alloc_direct(proc_t* proc, size_t size) {
    mmap_area_t** _hole = find_va_hole(proc, size, 0x1000);
    mmap_area_t* hole = *_hole;
    hole->mtype = kernel_allocated_heap_data;
    allocate(hole->vastart, size, false, false, proc->pml4);
    return (void*)hole->vastart;
}


mmap_area_t* free_mmap_area(mmap_area_t* mm, mmap_area_t** pmma, proc_t* proc) {
    uint64_t use_count = __atomic_sub_fetch(&mm->count, 1, __ATOMIC_SEQ_CST);
    switch (mm->mtype) {
    case program_data:
    case stack_data:
    case heap_data:
    case kernel_allocated_heap_data: {
        deallocate(mm->vastart, mm->vaend-mm->vastart, proc->pml4);
    } break;
    case nondealloc_map:
        break;
    }
    mmap_area_t* mmn = mm->next;
    *pmma = mm->next;
    if (use_count == 0) {
        free(mm);
    }
    return mmn;
}

void proc_dealloc(uintptr_t mem) {
    cpu_t* cpu = get_current_cput();
    proc_spinlock_lock(&cpu->__cpu_lock);
    proc_spinlock_lock(&cpu->__cpu_sched_lock);
    proc_spinlock_lock(&__thread_modifier);

    proc_t* proc = cpu->ct->parent_process;
    proc_dealloc_direct(proc, mem);

    proc_spinlock_unlock(&__thread_modifier);
    proc_spinlock_unlock(&cpu->__cpu_lock);
    proc_spinlock_unlock(&cpu->__cpu_sched_lock);
}

void proc_dealloc_direct(proc_t* proc, uintptr_t mem) {
    mmap_area_t** _hole = mmap_area(proc, mem);
    mmap_area_t* hole = *_hole;
    if (hole != NULL) {
        free_mmap_area(hole, _hole, proc);
    }
}

static int cpy_array(int count, char*** a) {
    char** array = *a;
    char** na = malloc(8*(count+1));
    if (na == NULL)
        return ENOMEM_INTERNAL;

    int i = 0;
    for (; i<count; i++) {
        char* cpy = malloc(strlen(array[i])+1);
        if (cpy == NULL) {
            for (int j=0; j<i; j++) {
                free(na[j]);
            }
            free(na);
            return ENOMEM_INTERNAL;
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
    char** na = different_page_mem(p->pml4, proc_alloc_direct(p, 8*(count+1))); // TODO: possible bug with small align
    if (na == NULL)
        return ENOMEM_INTERNAL;

    int i = 0;
    for (; i<count; i++) {
        char* cpy = proc_alloc_direct(p, strlen(array[i])+1);
        if (cpy == NULL) {
            return ENOMEM_INTERNAL;
        }
        memcpy_dpgs(p->pml4, (uintptr_t)get_active_page(), cpy, array[i], strlen(array[i])+1);
        na[i] = cpy;
    }
    na[count] = NULL;
    *a = na;
    return 0;
}

void free_proc_memory(proc_t* proc) {
    mmap_area_t* mm = proc->mem_maps;
    mmap_area_t** pmm = &proc->mem_maps;
    while (mm != NULL) {
        mm = free_mmap_area(mm, pmm, proc);
    }
}

static void free_array(int count, char** a) {
    for (int i=0; i<count; i++) {
        free(a[i]);
    }
    free(a);
}

int create_process_base(uint8_t* image_data, int argc, char** argv,
        char** envp, proc_t** cpt, uint8_t asked_priority, registers_t* r) {
    proc_spinlock_lock(&__thread_modifier);
    uint8_t cpp = get_current_cput()->ct->parent_process->priority;
    proc_spinlock_unlock(&__thread_modifier);

    if (cpp > asked_priority) {
        return EINVAL;
    }

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

    proc_t* process = malloc(sizeof(proc_t));
    if (process == NULL) {
        return ENOMEM_INTERNAL;
    }
    memset(process, 0, sizeof(proc_t));

    process->__ob_lock = 0;
    process->process_list.data = process;
    process->pprocess = true;

    process->fds = create_array();
    if (process->fds == NULL) {
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->threads = create_array();
    if (process->fds == NULL) {
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->mem_maps = NULL;
    process->proc_random = rg_create_random_generator(get_unix_time());
    process->parent = NULL;
    process->priority = asked_priority;
    process->pml4 = create_pml4();
    if (process->pml4 == 0) {
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->futexes = create_uint64_table();

    if (process->futexes == NULL) {
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->input_buffer = create_queue_static(__message_getter);

    if (process->input_buffer == NULL) {
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->pq_input_buffer = create_queue_static(__message_getter);

    if (process->input_buffer == NULL) {
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->blocked_wait_messages = create_list_static(__message_getter);
    if (process->blocked_wait_messages == NULL) {
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        return ENOMEM_INTERNAL;
    }

    process->temp_processes = create_list_static(__process_get_function);
	if (process->blocked_wait_messages == NULL) {
		free_list(process->blocked_wait_messages);
		free_queue(process->pq_input_buffer);
		free_queue(process->input_buffer);
		destroy_table(process->futexes);
		destroy_array(process->threads);
		destroy_array(process->fds);
		free(process);
		return ENOMEM_INTERNAL;
	}

    thread_t* main_thread = malloc(sizeof(thread_t));
    if (main_thread == NULL) {
    	free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return ENOMEM_INTERNAL;
    }
    memset(main_thread, 0, sizeof(thread_t));
    main_thread->parent_process = process;
    main_thread->priority = asked_priority;
    main_thread->blocked = false;

    main_thread->continuation = malloc(sizeof(continuation_t));
    if (main_thread->continuation == NULL) {
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return ENOMEM_INTERNAL;
    }
    main_thread->continuation->present = false;

    main_thread->futex_block = create_list_static(__blocked_getter);
    if (main_thread->futex_block == NULL) {
        free(main_thread->continuation);
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return ENOMEM_INTERNAL;
    }

    array_push_data(process->threads, main_thread);

    err = load_elf_exec((uintptr_t)image_data, process);
    if (err == ELF_ERROR_ENOMEM) {
        err = ENOMEM_INTERNAL;
    } else if (err != 0) {
        err = EINVAL;
    }

    if (err != 0) {
        free_list(main_thread->futex_block);
        free(main_thread->continuation);
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return err;
    }

    char** argvu = argv;
    char** envpu = envp;
    if ((err = cpy_array_user(argc, &argvu, process)) != 0) {
        free_list(main_thread->futex_block);
        free(main_thread->continuation);
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return err;
    }
    if ((err = cpy_array_user(envc, &envpu, process)) != 0) {
        free_list(main_thread->futex_block);
        free(main_thread->continuation);
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return err;
    }

    main_thread->local_info = proc_alloc_direct(process, sizeof(tli_t));
    if (main_thread->local_info == NULL) {
        free_list(main_thread->futex_block);
        free(main_thread->continuation);
        free(main_thread);
        free_list(process->temp_processes);
        free_list(process->blocked_wait_messages);
        free_queue(process->pq_input_buffer);
        free_queue(process->input_buffer);
        destroy_table(process->futexes);
        destroy_array(process->threads);
        destroy_array(process->fds);
        free(process);
        // TODO: free process address page
        return ENOMEM_INTERNAL;
    }
    tli_t* li = different_page_mem(process->pml4, main_thread->local_info);
    li->self = main_thread->local_info;

    for (int i=0; i<MESSAGE_BUFFER_CNT; i++) {
        _message_t* m = &process->output_buffer[i];
        m->owner = process;
        m->used = false;
        m->message = proc_alloc_direct(process, 0x200000);
        if (m->message == NULL) {
            free_list(main_thread->futex_block);
            free(main_thread->continuation);
            free(main_thread);
            free_list(process->temp_processes);
            free_list(process->blocked_wait_messages);
            free_queue(process->pq_input_buffer);
            free_queue(process->input_buffer);
            destroy_table(process->futexes);
            destroy_array(process->threads);
            destroy_array(process->fds);
            free(process);
            // TODO: free process address page
            return ENOMEM_INTERNAL;
        }
    }

    process->argc = argc;
    process->argv = argvu;
    process->environ = envpu;

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
    main_thread->last_rflags = 0x200; // enable interrupts

    proc_spinlock_lock(&__proclist_lock);
    main_thread->tId = __atomic_add_fetch(&thread_id_num, 1, __ATOMIC_SEQ_CST);
    process->proc_id = ++process_id_num;
    list_push_right(processes, process);
    proc_spinlock_unlock(&__proclist_lock);

    li->t = main_thread->tId;
    main_thread->last_rdi = (ruint_t)(uintptr_t)process->argc;
    main_thread->last_rsi = (ruint_t)(uintptr_t)process->argv;
    main_thread->last_rdx = (ruint_t)(uintptr_t)process->environ;
    main_thread->blocked_list.data = main_thread;
    main_thread->schedule_list.data = main_thread;

    free_array(argc, argv);
    free_array(envc, envp);

    // TODO: add split option?
    main_thread->last_rax = process->proc_id;
    enschedule_best(main_thread);
    *cpt = process;
    return 0;
}

uintptr_t map_virtual_virtual(uintptr_t* _vastart, uintptr_t vaend, bool readonly) {
    uintptr_t vastart = *_vastart;
    uintptr_t vaoffset = vastart % 0x1000;
    vastart = ALIGN_DOWN(vastart, 0x1000);
    vaend = ALIGN_UP(vaend, 0x1000);

    proc_t* proc = get_current_process();

    mmap_area_t** _hole = find_va_hole(proc, vaend-vastart, 0x1000);
    mmap_area_t* hole = *_hole;
    if (hole == NULL) {
        return 0;
    }
    hole->mtype = kernel_allocated_heap_data;
    uintptr_t temporary = hole->vastart;
    if (!map_range(_vastart, vaend, &temporary, hole->vaend, true, readonly, false, proc->pml4)) {
        free_mmap_area(hole, _hole, proc);
        return 0;
    }
    return hole->vastart+vaoffset;
}

uintptr_t map_physical_virtual(puint_t* _vastart, puint_t vaend, bool readonly) {
    puint_t vastart = *_vastart;
    uintptr_t vaoffset = vastart % 0x1000;
    vastart = ALIGN_DOWN(vastart, 0x1000);
    vaend = ALIGN_UP(vaend, 0x1000);

    proc_t* proc = get_current_process();

    mmap_area_t** _hole = find_va_hole(proc, vaend-vastart, 0x1000);
    mmap_area_t* hole = *_hole;
    if (hole == NULL) {
        return 0;
    }
    hole->mtype = kernel_allocated_heap_data;
    uintptr_t temporary = hole->vastart;
    if (!map_range(_vastart, vaend, &temporary, hole->vaend, false, readonly, false, proc->pml4)) {
        *_vastart = vastart;
        free_mmap_area(hole, _hole, proc);
        return 0;
    }
    return hole->vastart+vaoffset;
}

int cp_stage_1(cp_stage1* data, ruint_t* process_num) {
	int error = 0;
	*process_num = 0;
	proc_t* cp = get_current_process();

	temp_proc_t* tp = malloc(sizeof(temp_proc_t));
	if (tp == NULL) {
		goto handle_mem_error;
	}

	proc_t* process = malloc(sizeof(proc_t));
	if (process == NULL) {
		goto handle_mem_error;
	}
	memset(process, 0, sizeof(proc_t));

	tp->cstate = CP1;
	tp->process = process;

	process->__ob_lock = 0;
	process->process_list.data = process;

	if (data->privilege && cp->pprocess)
		process->pprocess = true;
	else
		process->pprocess = false;

	process->fds = create_array();
	if (process->fds == NULL) {
		goto handle_mem_error;
	}

	process->threads = create_array();
	if (process->fds == NULL) {
		destroy_array(process->fds);
		goto handle_mem_error;
	}

	process->mem_maps = NULL;
	process->proc_random = rg_create_random_generator(get_unix_time());
	process->parent = NULL;
	if (data->parent)
		process->parent = cp;
	if (data->priority < 0) {
		process->priority = cp->priority;
	} else if (data->priority > 4) {
		error = EINVAL;
		goto handle_mem_error;
	} else
		process->priority = data->priority;

	process->futexes = create_uint64_table();

	if (process->futexes == NULL) {
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}

	process->input_buffer = create_queue_static(__message_getter);

	if (process->input_buffer == NULL) {
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}

	process->pq_input_buffer = create_queue_static(__message_getter);

	if (process->input_buffer == NULL) {
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}

	process->blocked_wait_messages = create_list_static(__message_getter);
	if (process->blocked_wait_messages == NULL) {
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}

	process->temp_processes = create_list_static(__process_get_function);
	if (process->blocked_wait_messages == NULL) {
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}

	proc_spinlock_lock(&__proclist_lock);
	process->proc_id = ++process_id_num;
	proc_spinlock_unlock(&__proclist_lock);

	proc_spinlock_lock(&__proclist_lock2);
	if (table_set(temp_processes, (void*)process->proc_id, tp)) {
		proc_spinlock_unlock(&__proclist_lock2);
		error = ENOMEM_INTERNAL;
		goto handle_mem_error;
	}
	list_push_right(cp->temp_processes, process);
	proc_spinlock_unlock(&__proclist_lock2);

	return 0;
handle_mem_error:
	if (tp != NULL)	free(tp);
	if (process != NULL) {
		if (process->fds != NULL)
			destroy_array(process->fds);
		if (process->threads != NULL)
			destroy_array(process->threads);
		if (process->futexes != NULL)
			destroy_table(process->futexes);
		if (process->input_buffer != NULL)
			free_queue(process->input_buffer);
		if (process->pq_input_buffer != NULL)
			free_queue(process->pq_input_buffer);
		if (process->blocked_wait_messages != NULL)
			free_list(process->blocked_wait_messages);
		if (process->temp_processes != NULL)
			free_list(process->temp_processes);
		free(process);
	}
	return error;
}
