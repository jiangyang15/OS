#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_evict() {
	// record clock hand
	static int clock_hand = 0;

	int frame = -1;
	
	while(1){
		if (coremap[clock_hand].pte->frame & PG_REF){
			coremap[clock_hand].pte->frame &= ~PG_REF;
		}
		else {
			frame = clock_hand;
		}
		clock_hand = (clock_hand + 1) % memsize;

		// find 
		if (frame != -1){
			break;
		}
	}
	return frame;
}

/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {
	// already  set PG_REF
	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm. 
 */
void clock_init() {
}
