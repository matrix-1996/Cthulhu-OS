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
 * ny_initramfs.c
 *  Created on: Jan 6, 2016
 *      Author: Peter Vanusanik
 *  Contents: 
 */

#include "ny_initramfs.h"
#include <cthulhu/process.h>

int get_directory(const char* path, ifs_directory_t* dir) {
    int status;
    status =  dev_sys_2arg(DEV_SYS_IVFS_GET_PATH_ELEMENT, (ruint_t)path, (ruint_t)&dir->entry);
    if (status != E_IFS_ACTION_SUCCESS)
        return status;
    if (dir->entry.type != et_dir) {
        return E_IFS_NOT_A_DECTYPE;
    }
    return E_IFS_ACTION_SUCCESS;
}

int get_file(const char* path, ifs_file_t* file) {
    int status;
    status = dev_sys_2arg(DEV_SYS_IVFS_GET_PATH_ELEMENT, (ruint_t)path, (ruint_t)&file->entry);
    if (status != E_IFS_ACTION_SUCCESS)
        return status;
    if (file->entry.type != et_file) {
        return E_IFS_NOT_A_DECTYPE;
    }
    return E_IFS_ACTION_SUCCESS;
}

int execve_ifs(const char* ifs_path, char** argv, char** envp, int priority) {
    if (priority < 0 || priority >= 5)
        return EINVAL;

    int argc = 0;
    char** argt = argv;
    while (*argt != NULL) {
        ++argc;
        ++argt;
    }

    return dev_sys_4arg(DEV_SYS_INITRAMFS_EXECVE,
            (ruint_t)ifs_path, (ruint_t)argc, (ruint_t)argv, (ruint_t)envp);
}

