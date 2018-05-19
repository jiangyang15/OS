#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"
#include <sys/time.h>


extern int memsize;

extern int debug;

extern struct frame *coremap;

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {
	// find min timestamp
	long long min_timestamp = -1;
	int frame = 0;
	for (int i=0; i < memsize; ++i){
		// filter  no  timestamp
		if (coremap[i].timestamp != -1){
			// find min timestamp
			if (min_timestamp == -1 || min_timestamp > coremap[i].timestamp){
				min_timestamp = coremap[i].timestamp;
				frame = i;
			}
		}
	}
	// min timestamp   is   Least Recently Used
	return frame;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {

	int frame = p->frame >> PAGE_SHIFT;
	// if referenced,then update timestamp
	struct timeval t; 
   	gettimeofday(&t, NULL); 
    coremap[frame].timestamp = t.tv_sec*1000000 + t.tv_usec;
	return;
}


/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
	for (int i=0; i < memsize; ++i){
		coremap[i].timestamp = -1;
	}
}
