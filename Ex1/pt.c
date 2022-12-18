#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


#include "os.h"

uint64_t keep_bits(uint64_t vpn, int level) {
    if (level == 4) {
        return ((vpn) & 511);
    }
    return ((vpn >> (36-(level*9))) & 511);
}

/* page_table_update creates/destroys virtual memory mappings in a page table */
void page_table_update(uint64_t pt, uint64_t vpn, uint64_t ppn) {
    uint64_t shift_pt = pt << 12; /* shift the pt to get rid of the offset and valid bits */
    uint64_t* trie = phys_to_virt(shift_pt); /* get the vpn for the root of the pt */
    uint64_t pt_entry;
    for (int i = 0; i < 5; i++) { /* 9 bits symbol with a 45 bits VPN so we use a 5-level pt */
        pt_entry = keep_bits(vpn, i); /* keeping only the relevant vpn[9*i:9*i+9] bits for each level */
        if (i < 4) {
            if (!(trie[pt_entry] & 1)) { /* if no valid mapping is found */
                if (ppn == NO_MAPPING) {
                    return; /* preventing unneeded memory allocation */
                }
                uint64_t allocated = alloc_page_frame();
                trie[pt_entry] = (allocated << 12) + 1; /* shifting for offset and adding the valid bit */
            }
            trie = phys_to_virt(trie[pt_entry])-1; /* moving to the next level of the trie */
        }
    }
    if (ppn == NO_MAPPING) {
        trie[pt_entry] = trie[pt_entry] & 0;
    } else {
        trie[pt_entry] = (ppn << 12) + 1;
    }
    return;
}

/* page_table_query querys the mapping of a virtual page number in a page table */
uint64_t page_table_query(uint64_t pt, uint64_t vpn) {
    uint64_t shift_pt = pt << 12; 
    uint64_t* trie = phys_to_virt(shift_pt); 
    uint64_t pt_entry;
    for (int i = 0; i < 5; i++) { 
        pt_entry = keep_bits(vpn, i); 
        if (!(trie[pt_entry] & 1)) { /* if the valid bit was set to 0 */
            return NO_MAPPING;
        }
        if (i < 4) {
            trie = phys_to_virt(trie[pt_entry] - 1); /* moving to the next level of the trie */
        }
    }
    uint64_t ppn = trie[pt_entry] >> 12;
    return ppn;
}