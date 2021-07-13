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
 * @file        k_task.c
 * @brief       task management C file
 * @version     V1.2021.05
 * @authors     Yiqing Huang
 * @date        2021 MAY
 *
 * @attention   assumes NO HARDWARE INTERRUPTS
 * @details     The starter code shows one way of implementing context switching.
 *              The code only has minimal sanity check.
 *              There is no stack overflow check.
 *              The implementation assumes only three simple tasks and
 *              NO HARDWARE INTERRUPTS.
 *              The purpose is to show how context switch could be done
 *              under stated assumptions.
 *              These assumptions are not true in the required RTX Project!!!
 *              Understand the assumptions and the limitations of the code before
 *              using the code piece in your own project!!!
 *
 *****************************************************************************/


#include "k_inc.h"
#include "k_task.h"
#include "k_rtx.h"

/*
 *==========================================================================
 *                            GLOBAL VARIABLES
 *==========================================================================
 */

TCB             *gp_current_task = NULL;    // the current RUNNING task
TCB             g_tcbs[MAX_TASKS];          // an array of TCBs
//TASK_INIT       g_null_task_info;           // The null task info
U32             g_num_active_tasks = 0;     // number of non-dormant tasks

Queue array_of_queue[PRIORITY_NUM];
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
 *                            FUNCTIONS
 *===========================================================================
 */

/**************************************************************************//**
 * @brief   	SVC Handler
 * @pre         PSP is used in thread mode before entering SVC Handler
 *              SVC_Handler is configured as the highest interrupt priority
 *****************************************************************************/

void SVC_Handler(void)
{
    
    U8   svc_number;
    U32  ret  = RTX_OK;                 // default return value of a function
    U32 *args = (U32 *) __get_PSP();    // read PSP to get stacked args
    
    svc_number = ((S8 *) args[6])[-2];  // Memory[(Stacked PC) - 2]
    switch(svc_number) {
        case SVC_RTX_INIT:
            ret = k_rtx_init((RTX_SYS_INFO*) args[0], (TASK_INIT *) args[1], (int) args[2]);
            break;
        case SVC_MEM_ALLOC:
            ret = (U32) k_mpool_alloc(MPID_IRAM1, (size_t) args[0]);
            break;
        case SVC_MEM_DEALLOC:
            ret = k_mpool_dealloc(MPID_IRAM1, (void *)args[0]);
            break;
        case SVC_MEM_DUMP:
            ret = k_mpool_dump(MPID_IRAM1);
            break;
        case SVC_TSK_CREATE:
            ret = k_tsk_create((task_t *)(args[0]), (void (*)(void))(args[1]), (U8)(args[2]), (U32) (args[3]));
            break;
        case SVC_TSK_EXIT:
            k_tsk_exit();
            break;
        case SVC_TSK_YIELD:
            ret = k_tsk_yield();
            break;
        case SVC_TSK_SET_PRIO:
            ret = k_tsk_set_prio((task_t) args[0], (U8) args[1]);
            break;
        case SVC_TSK_GET:
            ret = k_tsk_get((task_t ) args[0], (RTX_TASK_INFO *) args[1]);
            break;
        case SVC_TSK_GETTID:
            ret = k_tsk_gettid();
            break;
        default:
            ret = (U32) RTX_ERR;
    }
    
    args[0] = ret;      // return value saved onto the stacked R0
}

/**************************************************************************//**
 * @brief   scheduler, pick the TCB of the next to run task
 *
 * @return  TCB pointer of the next to run task
 * @post    gp_current_task is updated
 * @note    you need to change this one to be a priority scheduler
 *
 *****************************************************************************/

TCB *scheduler(void)
{
    U8 next_tid = gp_current_task->tid;
    U8 current_highest_prio = current_priority_level();
    if(current_highest_prio == PRIO_NULL){
        return &g_tcbs[TID_NULL];
    }
    next_tid = pop(&(array_of_queue[current_highest_prio-0x80]));
    return &g_tcbs[next_tid];

}

/**
 * @brief initialzie the first task in the system
 */
void k_tsk_init_first(TASK_INIT *p_task)
{
    p_task->prio         = PRIO_NULL;
    p_task->priv         = 0;
    p_task->tid          = TID_NULL;
    p_task->ptask        = &task_null;
    p_task->u_stack_size = PROC_STACK_SIZE;
}

/**************************************************************************//**
 * @brief       initialize all boot-time tasks in the system,
 *
 *
 * @return      RTX_OK on success; RTX_ERR on failure
 * @param       task_info   boot-time task information structure pointer
 * @param       num_tasks   boot-time number of tasks
 * @pre         memory has been properly initialized
 * @post        none
 * @see         k_tsk_create_first
 * @see         k_tsk_create_new
 *****************************************************************************/

int k_tsk_init(TASK_INIT *task, int num_tasks)
{
    if (num_tasks > MAX_TASKS - 1) {
        return RTX_ERR;
    }
    
    TASK_INIT taskinfo;
    for(int i=0; i< MAX_TASKS; i++){
        g_tcbs[i].state = 4;
    }
    queue_init();
    k_tsk_init_first(&taskinfo);

    if ( k_tsk_create_new(&taskinfo, &g_tcbs[TID_NULL], TID_NULL) == RTX_OK ) {
        g_num_active_tasks = 1;
        gp_current_task = &g_tcbs[TID_NULL];
        push_back(&(array_of_queue[4]), gp_current_task->tid);
    } else {
        g_num_active_tasks = 0;
        return RTX_ERR;
    }
    
    // create the rest of the tasks
    for ( int i = 0; i < num_tasks; i++ ) {
        TCB *p_tcb = &g_tcbs[i+1];
        if (k_tsk_create_new(&task[i], p_tcb, i+1) == RTX_OK) {
            push_back(&(array_of_queue[task[i].prio-0x80]), p_tcb->tid);
            g_num_active_tasks++;
            //push_back(&(array_of_queue[task[i].prio-0x80]), p_tcb->tid);
        }
    }
    gp_current_task = scheduler();
    gp_current_task->state = RUNNING;
    return RTX_OK;
}
/**************************************************************************//**
 * @brief       initialize a new task in the system,
 *              one dummy kernel stack frame, one dummy user stack frame
 *
 * @return      RTX_OK on success; RTX_ERR on failure
 * @param       p_taskinfo  task initialization structure pointer
 * @param       p_tcb       the tcb the task is assigned to
 * @param       tid         the tid the task is assigned to
 *
 * @details     From bottom of the stack,
 *              we have user initial context (xPSR, PC, SP_USR, uR0-uR3)
 *              then we stack up the kernel initial context (kLR, kR4-kR12, PSP, CONTROL)
 *              The PC is the entry point of the user task
 *              The kLR is set to SVC_RESTORE
 *              20 registers in total
 * @note        YOU NEED TO MODIFY THIS FILE!!!
 *****************************************************************************/
int k_tsk_create_new(TASK_INIT *p_taskinfo, TCB *p_tcb, task_t tid)
{
    extern U32 SVC_RTE;

    U32 *usp;
    U32 *ksp;

    if (p_taskinfo == NULL || p_tcb == NULL)
    {
        return RTX_ERR;
    }

    p_tcb->tid   = tid;
    p_tcb->state = READY;
    p_tcb->prio  = p_taskinfo->prio;
    p_tcb->priv  = p_taskinfo->priv;
    
    /*---------------------------------------------------------------
     *  Step1: allocate user stack for the task
     *         stacks grows down, stack base is at the high address
     * ATTENTION: you need to modify the following three lines of code
     *            so that you use your own dynamic memory allocator
     *            to allocate variable size user stack.
     * -------------------------------------------------------------*/
    
    //usp = k_alloc_p_stack(tid);             // ***you need to change this line***
    int size_of_stack = PROC_STACK_SIZE;
    if(PROC_STACK_SIZE >= p_taskinfo->u_stack_size){
        usp = k_mpool_alloc(MPID_IRAM2, PROC_STACK_SIZE);
        if(usp == NULL){
            errno = ENOMEM;
            return RTX_ERR;
        }
    }else{
        usp = k_mpool_alloc(MPID_IRAM2, p_taskinfo->u_stack_size);
        if(usp == NULL){
            errno = ENOMEM;
            return RTX_ERR;
        }
        while(size_of_stack < p_taskinfo->u_stack_size){
            size_of_stack = size_of_stack << 1;
        }
    }
    usp = (U32*)((U32)usp + (U32)size_of_stack);
    p_tcb->tid = tid;
    p_tcb->state = READY;
    p_tcb->prio = p_taskinfo->prio;
    p_tcb->priv = p_taskinfo->priv;
    p_tcb->u_stack_size = size_of_stack;
    p_tcb->u_sp_base = usp;


    /*-------------------------------------------------------------------
     *  Step2: create task's thread mode initial context on the user stack.
     *         fabricate the stack so that the stack looks like that
     *         task executed and entered kernel from the SVC handler
     *         hence had the exception stack frame saved on the user stack.
     *         This fabrication allows the task to return
     *         to SVC_Handler before its execution.
     *
     *         8 registers listed in push order
     *         <xPSR, PC, uLR, uR12, uR3, uR2, uR1, uR0>
     * -------------------------------------------------------------*/

    // if kernel task runs under SVC mode, then no need to create user context stack frame for SVC handler entering
    // since we never enter from SVC handler in this case
    
    *(--usp) = INITIAL_xPSR;             // xPSR: Initial Processor State
    *(--usp) = (U32) (p_taskinfo->ptask);// PC: task entry point
        
    // uR14(LR), uR12, uR3, uR3, uR1, uR0, 6 registers
    for ( int j = 0; j < 6; j++ ) {
        
#ifdef DEBUG_0
        *(--usp) = 0xDEADAAA0 + j;
#else
        *(--usp) = 0x0;
#endif
    }
    p_tcb->usp = (U32)usp;
    // allocate kernel stack for the task
    ksp = k_alloc_k_stack(tid);
    if ( ksp == NULL ) {
        return RTX_ERR;
    }

    /*---------------------------------------------------------------
     *  Step3: create task kernel initial context on kernel stack
     *
     *         12 registers listed in push order
     *         <kLR, kR4-kR12, PSP, CONTROL>
     * -------------------------------------------------------------*/
    // a task never run before directly exit
    *(--ksp) = (U32) (&SVC_RTE);
    // kernel stack R4 - R12, 9 registers
#define NUM_REGS 9    // number of registers to push
      for ( int j = 0; j < NUM_REGS; j++) {        
#ifdef DEBUG_0
        *(--ksp) = 0xDEADCCC0 + j;
#else
        *(--ksp) = 0x0;
#endif
    }
        
    // put user sp on to the kernel stack
    *(--ksp) = (U32) usp;
    
    // save control register so that we return with correct access level
    if (p_taskinfo->priv == 1) {  // privileged 
        *(--ksp) = __get_CONTROL() & ~BIT(0); 
    } else {                      // unprivileged
        *(--ksp) = __get_CONTROL() | BIT(0);
    }

    p_tcb->msp = ksp;

    return RTX_OK;
}

/**************************************************************************//**
 * @brief       switching kernel stacks of two TCBs
 * @param       p_tcb_old, the old tcb that was in RUNNING
 * @return      RTX_OK upon success
 *              RTX_ERR upon failure
 * @pre         gp_current_task is pointing to a valid TCB
 *              gp_current_task->state = RUNNING
 *              gp_current_task != p_tcb_old
 *              p_tcb_old == NULL or p_tcb_old->state updated
 * @note        caller must ensure the pre-conditions are met before calling.
 *              the function does not check the pre-condition!
 * @note        The control register setting will be done by the caller
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * @attention   CRITICAL SECTION
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 *****************************************************************************/
__asm void k_tsk_switch(TCB *p_tcb_old)
{
        PRESERVE8
        EXPORT  K_RESTORE
        
        PUSH    {R4-R12, LR}                // save general pupose registers and return address
        MRS     R4, CONTROL                 
        MRS     R5, PSP
        PUSH    {R4-R5}                     // save CONTROL, PSP
        STR     SP, [R0, #TCB_MSP_OFFSET]   // save SP to p_old_tcb->msp
K_RESTORE
        LDR     R1, =__cpp(&gp_current_task)
        LDR     R2, [R1]
        LDR     SP, [R2, #TCB_MSP_OFFSET]   // restore msp of the gp_current_task
        POP     {R4-R5}
        MSR     PSP, R5                     // restore PSP
        MSR     CONTROL, R4                 // restore CONTROL
        ISB                                 // flush pipeline, not needed for CM3 (architectural recommendation)
        POP     {R4-R12, PC}                // restore general purpose registers and return address
}


__asm void k_tsk_start(void)
{
        PRESERVE8
        B K_RESTORE
}

/**************************************************************************//**
 * @brief       run a new thread. The caller becomes READY and
 *              the scheduler picks the next ready to run task.
 * @return      RTX_ERR on error and zero on success
 * @pre         gp_current_task != NULL && gp_current_task == RUNNING
 * @post        gp_current_task gets updated to next to run task
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * @attention   CRITICAL SECTION
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *****************************************************************************/
int k_tsk_run_new(void)
{
    TCB *p_tcb_old = NULL;
    
    if (gp_current_task == NULL) {
        return RTX_ERR;
    }

    p_tcb_old = gp_current_task;
    gp_current_task = scheduler();
    
    if ( gp_current_task == NULL  ) {
        gp_current_task = p_tcb_old;        // revert back to the old task
        return RTX_ERR;
    }

    // at this point, gp_current_task != NULL and p_tcb_old != NULL
    if (gp_current_task != p_tcb_old) {
        gp_current_task->state = RUNNING;   // change state of the to-be-switched-in  tcb
        if(p_tcb_old->state != DORMANT){
            p_tcb_old->state = READY;
            p_tcb_old->usp = __get_PSP();
        }           // change state of the to-be-switched-out tcb
        k_tsk_switch(p_tcb_old);            // switch kernel stacks       
    }

    return RTX_OK;
}

 
/**************************************************************************//**
 * @brief       yield the cpu
 * @return:     RTX_OK upon success
 *              RTX_ERR upon failure
 * @pre:        gp_current_task != NULL &&
 *              gp_current_task->state = RUNNING
 * @post        gp_current_task gets updated to next to run task
 * @note:       caller must ensure the pre-conditions before calling.
 *****************************************************************************/
int k_tsk_yield(void)
{
    if(is_empty(&(array_of_queue[find_which_queue(gp_current_task->prio)])) == 0){
        push_back(&(array_of_queue[find_which_queue(gp_current_task->prio)]), gp_current_task->tid);
        return k_tsk_run_new();
    }
    return RTX_OK;
}

/**
 * @brief   get task identification
 * @return  the task ID (TID) of the calling task
 */
task_t k_tsk_gettid(void)
{
    return gp_current_task->tid;
}

/*
 *===========================================================================
 *                             TO BE IMPLEMETED IN LAB2
 *===========================================================================
 */

int k_tsk_create(task_t *task, void (*task_entry)(void), U8 prio, U32 stack_size)
{
#ifdef DEBUG_0
    printf("k_tsk_create: entering...\n\r");
    printf("task = 0x%x, task_entry = 0x%x, prio=%d, stack_size = %d\n\r", task, task_entry, prio, stack_size);
#endif /* DEBUG_0 */
    if (task == NULL || task_entry == NULL || (prio != HIGH && prio != MEDIUM && prio != LOW && prio != LOWEST)) {
        errno = EINVAL;
        return RTX_ERR;
    }

    if (g_num_active_tasks == MAX_TASKS) {
        errno = EAGAIN;
        return RTX_ERR;
    }
    TASK_INIT taskinfo;
    task_t tid = 0;

    for (int i = 1; i < MAX_TASKS; ++i) {
        if ( (g_tcbs[i].state != READY) && (g_tcbs[i].state != RUNNING)) {
            tid = i;
            break;
        }
    }
    taskinfo.ptask = task_entry;
    taskinfo.prio = prio;
    taskinfo.priv = 0;
    taskinfo.u_stack_size = stack_size;

    if (k_tsk_create_new(&taskinfo, &g_tcbs[tid], tid) == RTX_OK) {
        g_num_active_tasks++;
        push_back(&(array_of_queue[prio-0x80]), tid);
		*task = tid;
    } else {
        return RTX_ERR;
    }

    if(prio < gp_current_task->prio){
        push_front(&(array_of_queue[find_which_queue(gp_current_task->prio)]), gp_current_task->tid);
        k_tsk_run_new();
    }

    return RTX_OK;
}

void k_tsk_exit(void) 
{
#ifdef DEBUG_0
    printf("k_tsk_exit: entering...\n\r");
#endif /* DEBUG_0 */
    if(gp_current_task->tid == TID_NULL){
        printf("try exit the null task\n");
        return;
    }
    gp_current_task -> state = DORMANT;
    k_mpool_dealloc(MPID_IRAM2, (U32*)((U32)gp_current_task->u_sp_base-(U32)gp_current_task->u_stack_size));
    gp_current_task->u_stack_size=0;
    gp_current_task->u_sp_base=NULL;
    gp_current_task->usp=0;
    g_num_active_tasks--;
    k_tsk_run_new();
    return;
}

int k_tsk_set_prio(task_t task_id, U8 prio) 
{
#ifdef DEBUG_0
    printf("k_tsk_set_prio: entering...\n\r");
    printf("task_id = %d, prio = %d.\n\r", task_id, prio);
#endif /* DEBUG_0 */
    if(task_id == TID_NULL || prio == PRIO_NULL){
        errno = EPERM;
        return RTX_ERR;
    }
    if(task_id >= MAX_TASKS || (prio!=HIGH || prio!=MEDIUM || prio!=LOW || prio!=LOWEST) ){
        errno = EPERM;
        return RTX_ERR;
    }

    if(gp_current_task->priv == 0 && g_tcbs[task_id]. priv == 1){
        errno = EPERM;
        return RTX_ERR;
    }

    if(g_tcbs[task_id].state == RUNNING){
        if(current_priority_level() >= prio){
            gp_current_task->prio = prio;
            return RTX_OK;
        }else{
            gp_current_task->prio = prio;
            push_back(&(array_of_queue[prio-0x80]), gp_current_task->tid);
            return k_tsk_run_new();
        }
    }else if(g_tcbs[task_id].state == READY){
        if(g_tcbs[task_id].prio == prio){
            return RTX_OK;
        }
        find_and_delete(&(array_of_queue[find_which_queue(g_tcbs[task_id].prio)]), task_id);
        push_back(&(array_of_queue[find_which_queue(prio)]), task_id);
        if(gp_current_task->prio<=prio){
            g_tcbs[task_id].prio = prio;
            return RTX_OK;
        }else{
            g_tcbs[task_id].prio = prio;
            push_front(&(array_of_queue[find_which_queue(gp_current_task->prio)]), gp_current_task->tid);
            return k_tsk_run_new();
        }
    }else{
        errno = EPERM;
        return RTX_ERR;
    } 
}

/**
 * @brief   Retrieve task internal information 
 * @note    this is a dummy implementation, you need to change the code
 */
int k_tsk_get(task_t tid, RTX_TASK_INFO *buffer)
{
#ifdef DEBUG_0
    printf("k_tsk_get: entering...\n\r");
    printf("tid = %d, buffer = 0x%x.\n\r", tid, buffer);
#endif /* DEBUG_0 */    
    if (buffer == NULL || tid == TID_NULL || tid >= MAX_TASKS) {
        errno = EFAULT;
        return RTX_ERR;
    }

    if ( (g_tcbs[tid].state != DORMANT) && (g_tcbs[tid].state != READY) && (g_tcbs[tid].state != RUNNING)){
        errno = EINVAL;
        return RTX_ERR;
    }
    /* The code fills the buffer with some fake task information. 
       You should fill the buffer with correct information    */
    buffer -> tid             = tid;
    buffer -> prio            = g_tcbs[tid].prio;
    buffer -> u_stack_size    = g_tcbs[tid].u_stack_size;
    buffer -> priv            = g_tcbs[tid].priv;
    buffer -> ptask           = g_tcbs[tid].ptask;
    buffer -> k_sp_base       = (U32)g_k_stacks[tid + 1];
    buffer -> k_stack_size    = KERN_STACK_SIZE;
    buffer -> state           = g_tcbs[tid].state;
    buffer -> u_sp_base       = (U32)g_tcbs[tid].u_sp_base;


    if (k_tsk_gettid() == tid) {
        buffer -> u_sp = __get_PSP();
        buffer -> k_sp = __get_MSP();
    } else {
        buffer -> u_sp = g_tcbs[tid].usp;
        buffer -> k_sp = (U32)g_tcbs[tid].msp;
    }
    return RTX_OK;     
}

int k_tsk_ls(task_t *buf, int count){
#ifdef DEBUG_0
    printf("k_tsk_ls: buf=0x%x, count=%d\r\n", buf, count);
#endif /* DEBUG_0 */
    return 0;
}


int is_empty(Queue* q){
    return q->size == 0;
}

void queue_init(void){
    for (int i=0; i<= PRIORITY_NUM; i++){
        array_of_queue[i].start = NULL;
        array_of_queue[i].end = NULL;
        array_of_queue[i].size = 0;
    }
}

void queue_free(void){
    
}

task_t pop(Queue* q){
    if(q->size <= 0){
        printf("size < 0!\n");
    }
    
    TaskNode* temp = q->start;
    task_t result = temp->tid;
    //printf("result:%d\n",result);
    q->start = temp->next;
    k_mpool_dealloc(MPID_IRAM2, (void*)temp);
    (q->size)--;
    if(q->size == 0){
        q->start = NULL;
        q->end = NULL;
    }
    return result;
}

void push_back(Queue* q, task_t tid){
    if(q==NULL) return;
    TaskNode* temp = (TaskNode*)k_mpool_alloc(MPID_IRAM2, sizeof(TaskNode));
    temp->tid = tid;
    //printf("temptid%d, tid%d\n", temp->tid, tid);
    //printf("temp%lu\n",(*temp));
    temp->next = NULL;
    if(q->size == 0){
        q->start = temp;
        q->end = temp;
    }else{
        q->end->next = temp;
        q->end = temp;
    }
    (q->size)++;
}

void push_front(Queue* q, task_t tid){
    TaskNode* temp = (TaskNode*)k_mpool_alloc(MPID_IRAM2, sizeof(TaskNode));
    temp->tid = tid;
    temp->next = NULL;
    if(q->size == 0){
        q->start = q->end = temp;
    }else{
        temp->next = q->start;
        q->start = temp;
    }
    (q->size)++;
}

int is_inside_queue(Queue* q, task_t tid){
    TaskNode* temp = q->start;
    while(temp != NULL && temp->tid != tid){
        temp = temp->next;
    }
    if(temp != NULL){
        return 1;
    }else return 0;
}

int find_which_queue(task_t tid){
    for(int i= 0; i< PRIORITY_NUM; i++){
        if(is_inside_queue(&(array_of_queue[i]), tid)) return i;
    }
    return -1;
}

int current_priority_level(void){
    for(int i= 0; i< PRIORITY_NUM; i++){
        //printf("level%d\n",i);
        if(array_of_queue[i].size > 0) return i;
    }
    return -1;
}

void find_and_delete(Queue* q, task_t tid){
    TaskNode* temp = q->start;
    TaskNode* temp_pre = NULL;
    while(temp != NULL && temp->tid != tid){
        temp_pre = temp;
        temp = temp->next;
    }
    if(temp != NULL){
        if(temp_pre == NULL){
            pop(q);
        }else{
            temp_pre->next = temp->next;
            k_mpool_dealloc(MPID_IRAM2, (void*)temp);
        }
    }
}

/*
 *===========================================================================
 *                             END OF FILE
 *===========================================================================
 */

