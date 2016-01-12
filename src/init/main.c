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
 * main.c
 *  Created on: Jan 2, 2016
 *      Author: Peter Vanusanik
 *  Contents: 
 */

#include <ny/nyarlathotep.h>
#include <sys/unistd.h>



int main(void) {
	pid_t pid = getpid();

	ifs_file_t f;
	get_file("init/dorder", &f);

	char* contents = malloc(f.entry.num_ent_or_size+1);
	memcpy(contents, f.file_contents, f.entry.num_ent_or_size);
	contents[f.entry.num_ent_or_size] = 0;

	char* daemon_path = strtok(contents, "\n");
	while (daemon_path != NULL) {
		if (strlen(daemon_path)==0)
			break;
		if (fork() != pid) {

			char** args = malloc(16);
			char* arg = malloc(strlen(daemon_path)+1);
			memcpy(arg, daemon_path, strlen(daemon_path)+1);

			args[0] = arg;
			args[1] = NULL;

			char** envp = malloc(8);
			envp[0] = NULL;

			execve_ifs(daemon_path, args, envp);

			while (1) ; // sentinel break, TODO: add abort
		} else {
			daemon_path = strtok(NULL, "\n");
		}
	}

    while (1) ;
}
