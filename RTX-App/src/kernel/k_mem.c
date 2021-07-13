/*
 ****************************************************************************
 *
 *                  UNIVERSITY OF WATERLOO ECE 350 RTOS LAB
 *
 *                     Copyright 2020-2021 Yiqing Huang
 *                          All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice and the following disclaimer.
 *
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************************
 */

/**************************************************************************//**
 * @file        k_mem.c
 * @brief       Kernel Memory Management API C Code
 *
 * @version     V1.2021.01.lab2
 * @authors     Yiqing Huang
 * @date        2021 JAN
 *
 * @note        skeleton code
 *
 *****************************************************************************/

#include "k_inc.h"
#include "k_mem.h"

/*---------------------------------------------------------------------------
The memory map of the OS image may look like the following:
                   RAM1_END-->+---------------------------+ High Address
                              |                           |
                              |                           |
                              |       MPID_IRAM1          |
                              |   (for user space heap  ) |
                              |                           |
                 RAM1_START-->|---------------------------|
                              |                           |
                              |  unmanaged free space     |
                              |                           |
&Image$$RW_IRAM1$$ZI$$Limit-->|---------------------------|-----+-----
                              |         ......            |     ^
                              |---------------------------|     |
                              |                           |     |
                              |---------------------------|     |
                              |                           |     |
                              |      other data           |     |
                              |---------------------------|     |
                              |      PROC_STACK_SIZE      |  OS Image
              g_p_stacks[2]-->|---------------------------|     |
                              |      PROC_STACK_SIZE      |     |
              g_p_stacks[1]-->|---------------------------|     |
                              |      PROC_STACK_SIZE      |     |
              g_p_stacks[0]-->|---------------------------|     |
                              |   other  global vars      |     |
                              |                           |  OS Image
                              |---------------------------|     |
                              |      KERN_STACK_SIZE      |     |                
             g_k_stacks[15]-->|---------------------------|     |
                              |                           |     |
                              |     other kernel stacks   |     |                              
                              |---------------------------|     |
                              |      KERN_STACK_SIZE      |  OS Image
              g_k_stacks[2]-->|---------------------------|     |
                              |      KERN_STACK_SIZE      |     |                      
              g_k_stacks[1]-->|---------------------------|     |
                              |      KERN_STACK_SIZE      |     |
              g_k_stacks[0]-->|---------------------------|     |
                              |   other  global vars      |     |
                              |---------------------------|     |
                              |        TCBs               |  OS Image
                      g_tcbs->|---------------------------|     |
                              |        global vars        |     |
                              |---------------------------|     |
                              |                           |     |          
                              |                           |     |
                              |        Code + RO          |     |
                              |                           |     V
                 IRAM1_BASE-->+---------------------------+ Low Address
    
---------------------------------------------------------------------------*/ 

/*
 *===========================================================================
 *                            GLOBAL VARIABLES
 *===========================================================================
 */
// kernel stack size, referred by startup_a9.s
const U32 g_k_stack_size = KERN_STACK_SIZE;
// task proc space stack size in bytes, referred by system_a9.c
const U32 g_p_stack_size = PROC_STACK_SIZE;

// task kernel stacks
U32 g_k_stacks[MAX_TASKS][KERN_STACK_SIZE >> 2] __attribute__((aligned(8)));

// task process stack (i.e. user stack) for tasks in thread mode
// remove this bug array in your lab2 code
// the user stack should come from MPID_IRAM2 memory pool
//U32 g_p_stacks[MAX_TASKS][PROC_STACK_SIZE >> 2] __attribute__((aligned(8)));
U32 g_p_stacks[NUM_TASKS][PROC_STACK_SIZE >> 2] __attribute__((aligned(8)));



const int NUM_LEVELS_RAM1 = (RAM1_SIZE_LOG2 - MIN_BLK_SIZE_LOG2) + 1;
DLIST *list1 = NULL; // Array of length NUM_LEVELS in unmanaged memory
U8 *tree1 = NULL; // Array of length NUM_TREE_BITS in unmanaged memory



const int NUM_LEVELS_RAM2 = (RAM2_SIZE_LOG2 - MIN_BLK_SIZE_LOG2) + 1;

DLIST list2[NUM_LEVELS_RAM2]; // Array of length NUM_LEVELS in unmanaged memory
U8 tree2[2047]={0}; // Array of length NUM_TREE_BITS in unmanaged memory hardcoded

/*
 *===========================================================================
 *                            FUNCTIONS
 *===========================================================================
 */

/* note list[n] is for blocks with order of n */
mpool_t k_mpool_create (int algo, U32 start, U32 end)
{
    mpool_t mpid = MPID_IRAM1;

#ifdef DEBUG_0
    printf("k_mpool_init: algo = %d\r\n", algo);
    printf("k_mpool_init: RAM range: [0x%x, 0x%x].\r\n", start, end);
#endif /* DEBUG_0 */    
    
    if (algo != BUDDY ) {
        errno = EINVAL;
        return RTX_ERR;
    }
    
    if ( start == RAM1_START) {
      // Setup first block that holds all memory
      DNODE *ptr = (void *) start; // 8-byte alignment not needed for this lab
			ptr->next = NULL;
			ptr->prev = NULL;
			ptr->treepos = 0;
			
			list1 = (void*)&Image$$RW_IRAM1$$ZI$$Limit;
			tree1 = (U8*)list1 + (sizeof(DLIST) * NUM_LEVELS_RAM1);
			int num_tree_bits = computer_pwr2(NUM_LEVELS_RAM1) - 1;
			
			for (int i = 0; i<num_tree_bits; i++)
			{
				tree1[i] = 0;
			}
			
			for (int i = 0; i<NUM_LEVELS_RAM1; i++)
			{
				list1[i].head = NULL;
			}
			
			list1[0].head = ptr;
			list1[0].tail = ptr;
			
    } else if ( start == RAM2_START) { 
      mpid = MPID_IRAM2;
      DNODE *ptr = (void *) start; // 8-byte alignment not needed for this lab
			ptr->next = NULL;
			ptr->prev = NULL;
			ptr->treepos = 0; 
			
			for (int i = 0; i<NUM_LEVELS_RAM2; i++)
			{
				list2[i].head = NULL;
			}
			
      list2[0].head = ptr;
			list2[0].tail = ptr;
    } else {
        errno = EINVAL;
        return RTX_ERR;
    }
    
    return mpid;
}

void *k_mpool_alloc (mpool_t mpid, size_t size)
{
#ifdef DEBUG_0
    printf("k_mpool_alloc: mpid = %d, size = %d, 0x%x\r\n", mpid, size, size);
#endif /* DEBUG_0 */
    
    if (size == 0) {
        return NULL;
    }
		if (size > IRAM1_SIZE) //************************** replace ***
		{
			errno = ENOMEM;
			return NULL;
		}

    if (mpid != MPID_IRAM1 && mpid != MPID_IRAM2) {
        errno = EINVAL;
        return NULL;
    }
		
		size_t blk_size = size;
    if (blk_size < MIN_BLK_SIZE)
    {
        blk_size = MIN_BLK_SIZE;
    }
   
		
		int lvl;
		DLIST *list; //list to use
		U8 *tree; //tree to use
		if (mpid == MPID_IRAM1)
		{
			unsigned int pwr = find_log(blk_size);
			lvl = RAM1_SIZE_LOG2 - pwr;
			list = list1;
			tree = tree1;
			if (size > RAM1_SIZE) 
			{
				errno = ENOMEM;
				return NULL;
			}
		}
		else
		{
			unsigned int pwr = find_log(blk_size);
			lvl = RAM2_SIZE_LOG2 - pwr;
			list = list2;
			tree = tree2;
			if (size > RAM2_SIZE) 
			{
				errno = ENOMEM;
				return NULL;
			}
		}	
		//exit if trying to alloc biggest block possible and it is occupied
		if ((lvl ==0)&&(list[lvl].head == NULL))
		{
			errno = ENOMEM;
			return NULL;			
		}
		// There is an exact block available.
		if(list[lvl].head != NULL)
		{
			void *memptr = list[lvl].head;

        // Remove head node from linked list, and remove any `prev` reference to it from next node
        DNODE *node = list[lvl].head;
        if (node->next) {
            node->next->prev = NULL;
        }
			list[lvl].head = node->next;
			int pos = node->treepos;
			node->next = NULL;
			node->prev = NULL;
			tree[pos] = 1;
			return memptr;
		}
		// No more blocks at that level, go up levels until we find one, then split it
		else
		{
			int k = 1;
		while((list[lvl-k].head == NULL) && (lvl-k>=0)) //test if i have the bounds right
		{
			if ((lvl-k==0) && (list[lvl-k].head == NULL))
			{
			//theres no free space.
				errno = ENOMEM;
				return NULL;
			}
			k++;
		}
		while (k > 0)
		{
			//split
			int pos = list[lvl-k].head->treepos;
			tree[pos] = 1;

			//need to know what block num it is at each lvl...
			int block_num = find_block_num(pos);
			int pwr = computer_pwr2(lvl-k+1);
			int child1 = pwr - 1 + 2*block_num;
			int child2 = child1+1;

			DNODE *ptr1 = list[lvl-k].head; //do some math to get mem ptr...
			DNODE *ptr2;
			if (mpid == MPID_IRAM1)
			{
				ptr2 = (DNODE*)((char*)ptr1 + RAM1_SIZE / pwr);
			}
			else
			{
				ptr2 = (DNODE*)((char*)ptr1 + RAM2_SIZE / pwr);
			}
			
			list[lvl-k].head = list[lvl-k].head->next;
			k--; //go down a lvl

			ptr1->next = ptr2;
			ptr1->prev = NULL;
			ptr2->next = NULL;
			ptr2->prev = ptr1;

			ptr1->treepos = child1;
			ptr2->treepos = child2;
			list[lvl-k].head = ptr1; //set
		}
		void *memptr = list[lvl].head;
    // Remove head node from linked list, and remove any `prev` reference to it from next node
    DNODE *node = list[lvl].head;
    if (node->next) {
        node->next->prev = NULL;
    }

		list[lvl].head = node->next;
		int pos = node->treepos;
		node->next = NULL;
		node->prev = NULL;
		tree[pos] = 1;
		return memptr;
	}
}

int k_mpool_dealloc(mpool_t mpid, void *ptr)
{
#ifdef DEBUG_0
    printf("k_mpool_dealloc: mpid = %d, ptr = 0x%x\r\n", mpid, ptr);
#endif /* DEBUG_0 */
    if (ptr == NULL) {
        return RTX_OK; // deallocating null is a no-op
    }
    if (mpid != MPID_IRAM1 && mpid != MPID_IRAM2) {
        errno = EINVAL;
        return RTX_ERR;
    }
    unsigned int offset = 0;
		
    if (((void*)RAM1_START <= ptr) && (ptr <= (void*)RAM1_END)) {
        //in RAM 1
        offset = (char*)ptr - (char*)RAM1_START;
    } else if (((void*)RAM2_START <= ptr) && (ptr <= (void*)RAM2_END)) {
    	//in RAM 2
        offset = (char*)ptr - (char*)RAM2_START;
    } else {
        errno = EFAULT;
        return RTX_ERR;
    }
	
		
		int pwr = computer_pwr2(5); //2^5 is the smallest block size
		int x = offset/pwr;
		int k;
		unsigned int treeIndex;		
		
		DLIST *list; //list to use
		U8 *tree; //tree to use
		if (mpid == MPID_IRAM1)
		{
			k = NUM_LEVELS_RAM1 -1;
			treeIndex = x + computer_pwr2(k) - 1;
			list = list1;
			tree = tree1;
		}
		else
		{
			k = NUM_LEVELS_RAM2 -1;
			treeIndex = x + computer_pwr2(k) - 1;
			list = list2;
			tree = tree2;
		}
		
		
		DNODE* ptr_to_use = ptr;		
		while((tree[treeIndex]!=1)&&(k>0))
		{
			k = k -1; //level
			x = x/2; //offset from lvl
			treeIndex = x + computer_pwr2(k) - 1;
		}
		
		if (treeIndex == 0)
		{
			DNODE *node1 = ptr; 
			node1->treepos = treeIndex;
			node1->next = NULL;
			node1->prev = NULL;
			tree[treeIndex] = 0;
			list[0].head= ptr;
			
		}
		else
		{
		
			while (k>0)
			{
			treeIndex = x + computer_pwr2(k) - 1;
			tree[treeIndex] = 0;
			//check the buddy now
			int buddyindex = 0;
			if (treeIndex&1)//odd
			{
				buddyindex = treeIndex+1;
			}
			else
			{
			//the initial node is even so the buddy is 1 behind
				buddyindex = treeIndex-1;
			}
		
			if (tree[buddyindex]==0)
			{
					//combine and go up one
					//align
				DNODE *remove = list[k].head; 
				int count = 0;
				while(remove->treepos != buddyindex)
				{
					remove = remove->next;
					count ++;
				}
				if (count == 0) //node to remove is at the front of list
				{
					if (list[k].head->next != NULL)
					{
						list[k].head->next->prev = NULL;
					}
					list[k].head = list[k].head->next; //maybe check if null should be fine anyways
				}
				else //node is somewhere else
				{
					remove->prev->next = remove->next;
					if (remove->next != NULL)
					{
						remove->next->prev = remove->prev;
					}
				}
				if (ptr_to_use>remove) //use the leftmost ptr?
				{
					ptr_to_use = remove;
				}
					k--;
					x=x/2;
				
				
					if ((buddyindex==1)&&(treeIndex==2))
					{
						DNODE *node1 = ptr_to_use; 
						node1->treepos = 0;
						node1->next = NULL;
						node1->prev = NULL;
						tree[treeIndex] = 0;
						list[0].head= ptr_to_use;
					}
					if ((buddyindex==2)&&(treeIndex==1))
					{
						DNODE *node1 = ptr_to_use; 
						node1->treepos = 0;
						node1->next = NULL;
						node1->prev = NULL;
						tree[treeIndex] = 0;
						list[0].head= ptr_to_use;
					}
			}
			else
			{
				DNODE *node1 = ptr_to_use; 
				node1->treepos = treeIndex;
				if (list[k].head == NULL)
				{
					node1->next = NULL;
					node1->prev = NULL;
					list[k].head = node1;
					
				}
				else
				{
					list[k].head->prev = node1;
					node1->prev = NULL;
					node1->next = list[k].head; 
					list[k].head = node1;
				}
				break;
			}
		}
	}

    return RTX_OK; 
}

int k_mpool_dump (mpool_t mpid)
{
#ifdef DEBUG_0
    printf("k_mpool_dump: mpid = %d\r\n", mpid);
#endif /* DEBUG_0 */
    if (mpid != MPID_IRAM1 && mpid != MPID_IRAM2)
	{
        errno = EINVAL;
        return NULL;
    }
    int total = 0;
    unsigned long size = 0;
		
		DLIST *list; //list to use
		int log_size;
		int max_lvls;
		if (mpid == MPID_IRAM1)
		{
		
			list = list1;
			log_size = RAM1_SIZE_LOG2;
			max_lvls = NUM_LEVELS_RAM1; 
		}
		else
		{
	
			list = list2;
			log_size = RAM2_SIZE_LOG2;
			max_lvls = NUM_LEVELS_RAM2; 
		}
		for (int k = 0; k < max_lvls; k++)
		{
			DNODE *temp = list[k].head;
			
			while(temp!=NULL)
			{
				size = computer_pwr2(log_size-k);
				printf("0x%x: 0x%x\r\n", temp, size);
				total++;
				temp=temp->next;	
			}
			
		}
		printf("%d free memory block(s) found\r\n", total);
    return total;
}
 
int k_mem_init(int algo)
{
#ifdef DEBUG_0
    printf("k_mem_init: algo = %d\r\n", algo);
#endif /* DEBUG_0 */
        
    if ( k_mpool_create(algo, RAM1_START, RAM1_END) < 0 ) {
        return RTX_ERR;
    }
    
    if ( k_mpool_create(algo, RAM2_START, RAM2_END) < 0 ) {
        return RTX_ERR;
    }
    
    return RTX_OK;
}

/**
 * @brief allocate kernel stack statically
 */
U32* k_alloc_k_stack(task_t tid)
{
    
    if ( tid >= MAX_TASKS) {
        errno = EAGAIN;
        return NULL;
    }
    U32 *sp = g_k_stacks[tid+1];
    
    // 8B stack alignment adjustment
    if ((U32)sp & 0x04) {   // if sp not 8B aligned, then it must be 4B aligned
        sp--;               // adjust it to 8B aligned
    }
    return sp;
}

/**
 * @brief allocate user/process stack statically
 * @attention  you should not use this function in your lab
 */

U32* k_alloc_p_stack(task_t tid)
{
    if ( tid >= NUM_TASKS ) {
        errno = EAGAIN;
        return NULL;
    }
    
    U32 *sp = g_p_stacks[tid+1];
    
    
    // 8B stack alignment adjustment
    if ((U32)sp & 0x04) {   // if sp not 8B aligned, then it must be 4B aligned
        sp--;               // adjust it to 8B aligned
    }
    return sp;
}


unsigned int find_log(size_t size)
{
	//parts from https://graphics.stanford.edu/~seander/bithacks.html
    unsigned int v = size;
    unsigned int r = 0;
    U8 flag = 0;
    unsigned int temp = v;
	//temp and flag used to determine if I need to add one more to r
	//eg 17 will have r = 4 (which is 16) so i need to add 1 to it
	if (temp&1)
    {
        flag++;
    }
    while (v >>= 1) // unroll for more speed...
    {
        if (flag != 2)
        {
            temp = v;
            if (temp&1)
            {
                flag++;
            }
        }
        r++;
    }
    if (flag == 2)
    {
        return r+1;
    }
    else
    {
        return r;
    }
}

unsigned int find_block_num(int treepos)
{
	//parts from https://graphics.stanford.edu/~seander/bithacks.html
	unsigned int v = treepos;
	v++; //to acount for starting at 0 at each level instead of 1.
	
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v = v - (v >> 1);
	return treepos - v + 1;
	
}

unsigned int computer_pwr2(int pwr)
{
	int x = 1; 
	for (int k=0; k<pwr; k++)
	{
		x = x << 1;
	}
	return x;
	
}

/*
 *===========================================================================
 *                             END OF FILE
 *===========================================================================
 */

