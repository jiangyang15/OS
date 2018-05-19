#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"

#define MAXLINE 256


extern int memsize;

extern int debug;

extern struct frame *coremap;

extern char  *tracefile;

int  file_line_size;  // tracefile  line number
addr_t * trace_file_vaddr;   //  record trace file virtual address

/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {

	int frame = 0;
	int max_dis = 0;

	// find max distance
	for (int i=0; i < memsize; ++i){
		if (max_dis < coremap[i].dis){
			max_dis = coremap[i].dis;
			frame = i;
		}
	}	
	
	return frame;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {

	int frame = p->frame >> PAGE_SHIFT;

	// record  current trace file line number
	static int file_line_idx = 0;

	addr_t cur_vaddr = trace_file_vaddr[file_line_idx];

	int dis = 0;

	//cur virtual address to next simple virtual address distance
	for (int i = file_line_idx + 1; i < file_line_size; ++i){
		if (cur_vaddr != trace_file_vaddr[i]){
			dis++;
		}
		else{
			break;
		}
	}
    
	coremap[frame].dis = dis;

	// move file index
	file_line_idx ++;
	return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
	file_line_size = 0;
	char buf[MAXLINE];
	addr_t vaddr = 0;
	char type;
	int i;
	FILE * tfp;

	for (i=0; i < memsize; ++i){
		coremap[i].dis = 0;
	}

	//read virtual address from tracefile
	if((tfp = fopen(tracefile, "r")) == NULL) {
		perror("Error opening tracefile:");
		exit(1);
	}

	// count total line number
	while(fgets(buf, MAXLINE, tfp) != NULL) {
		if(buf[0] != '=') {
			file_line_size ++;
		}
	}

	// malloc 
	trace_file_vaddr = (addr_t *)malloc(file_line_size * sizeof (addr_t));
	// recovery  file pointer 
	fseek(tfp, 0, SEEK_SET);
	//  read content
	i = 0;
	while(fgets(buf, MAXLINE, tfp) != NULL) {
		if(buf[0] != '=') {
			sscanf(buf, "%c %lx", &type, &vaddr);
			trace_file_vaddr[i] = vaddr;
			i++;
		}
	}
	fclose(tfp);
}

