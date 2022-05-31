#include "vm_pager.h"
#include <map>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string.h>
#include <list>

using namespace std;

/**********
data structure needed:
	free_phy_mem_list: a linked list (vector) for free physical mem page

	free_disk_block_list: a linked list (vector) for free disk block

	proc_vm_info: store vm info of a process
		pagetable: pagetable_t variable
		extra_info: a vector storing extra info of each valid virtual page
			val: a bit indicate whether this virtual page is valid
			res: whether the page resident in physical memory or disk
			ref: has been referenced
			dirt: has been written
			new: a newly allocated page but not filled with 0
			zero: a totally zero page (which need not to be paged out)
			disk_num: page-out

	clock_queue: a queue (double_linked list) for clock algorithm
		2 fields: pid, virtual_page_num
		when a virtual page comes resident append it to rear

	temp_disk_block_map: when a block paged in, it should not set free instantly but act as a temp for efficiency when the same page is non-modified and paged out again.
		key: pid, virtual_page_num
		value: block_num

	clock_pointer: point to eviction candidate
	current_pid

**********/

vector<unsigned long> free_phy_mem_page_list;
vector<unsigned long> free_disk_block_list;
typedef struct {
	unsigned int val : 1;
	unsigned int res : 1;
	unsigned int ref : 1;
	unsigned int dirt : 1;
	unsigned int init : 1;
	unsigned int zero : 1;
	unsigned long disk_num;
} page_extra_info;

typedef struct {
	pagetable_t pagetable;
	vector<page_extra_info> extra_info;
	int top_virtual_page_num;
} proc_vm_info;

map<pid_t, proc_vm_info *> vm_info;

typedef struct {
	pid_t pid;
	int page_num;
} virtual_page_indentifier;


list <virtual_page_indentifier> clock_queue;

list <virtual_page_indentifier>::iterator clock_pointer;

map<virtual_page_indentifier, unsigned long> temp_disk_block_map;

pid_t current_pid;



/**********
fuction definition

vm_init: Make a linked list of memory_pages and disk_blocks

vm_create(pid): create vm info of this process

vm_switch(pid): set current_pid and page_table_register
**********/

void 
vm_init(unsigned int memory_pages, unsigned int disk_blocks)
{
	printf("I am  initiating with %d pages memory and %d blocks disk\n",memory_pages, disk_blocks);
	for (int i = 0; i < memory_pages; i++) {
		free_phy_mem_page_list.push_back(i);
	}
	for (int i = 0; i < disk_blocks; i++) {
		free_disk_block_list.push_back(i);
	}

	clock_pointer = 0;
}

void 
vm_create(pid_t pid)
{
	proc_vm_info *info = (proc_vm_info *)calloc(1, sizeof(proc_vm_info));
	vm_info[pid] = info;
	printf("I'am creating %d\n", pid);
}

void 
vm_switch(pid_t pid)
{
	printf("I am switching to %d\n", pid);
	current_pid = pid;
	page_table_base_register = &(vm_info[pid]->page_table);
}

/**********
vm_extend()
	if there is free physical memory:
		pop free_phy_mem_list and add its page_num to pte of top_vm_page
		set new page extra_info: val=1, res=1, ref=0, dirt=0, new=1, zero=1, disk_num=0
		add (pid, top_vm_page) to clock queue
		update top_vm_page
		return new top_vm_page
	else if there is free disk block:
		set new page (via top_vm_page) extra_info: val=1, res=0, ref=0, dirt=0, new=1, zero=1
		pop free_disk_block_list add its block_num to extra_info.disk_num
		update top_vm_page
		return new top_vm_page
	else if there is temp disk block (!temp_disk_block_map.empty()):
		set new page (via top_vm_page) extra_info: val=1, res=0, ref=0, dirt=0, new=1, zero=1
		pop temp_disk_block_map and add its block_num to extra_info.disk_num
		update top_vm_page
		return new top_vm_page
	else:
		no avail mem or disk
		return 0
**********/
void * 
vm_extend()
{
	void *return_addr = 0;
	proc_vm_info *info = vm_info[current_pid];
	if (!free_phy_mem_page_list.empty()) {
		info->page_table.ptes[info->top_virtual_page_num].ppage=free_phy_mem_page_list.back();

		// val=1, res=1, ref=0, dirt=0, new=1, zero=1, disk_num=0
		page_extra_info ei = {1,1,0,0,1,1,0};
		info->extra_info.push_back(ei);

		free_phy_mem_page_list.pop_back();

		virtual_page_indentifier vpi = {current_pid, top_virtual_page_num}
		clock_queue.push_back(vpi);

		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;

	} else if (!free_disk_block_list.empty()) {
		// val=1, res=0, ref=0, dirt=0, new=1, zero=1, disk_num=..back()
		page_extra_info ei = {1,0,0,0,1,1,free_disk_block_list.back()};
		info->extra_info.push_back(ei);

		free_disk_block_list.pop_back();

		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;

	} else if (!temp_disk_block_map.empty()) {
		virtual_page_indentifier vpi = temp_disk_block_map.begin()->first;

		// val=1, res=0, ref=0, dirt=0, new=1, zero=1, disk_num=..second
		page_extra_info ei = {1,0,0,0,1,1,temp_disk_block_map.begin()->second};
		info->extra_info.push_back(ei);

		temp_disk_block_map.erase(vpi);

		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;
	}
	return return_addr;

}

void 
vm_destroy()
{
	proc_vm_info *info = vm_info[current_pid];
	for (int i = 0; i < info->top_virtual_page_num; i++) {
		
	}
}

int 
vm_fault(void *addr, bool write_flag)
{
	proc_vm_info *info = vm_info[current_pid];
    unsigned long int index = ((unsigned long int)addr - (unsigned long int)VM_ARENA_BASEADDR)/VM_PAGESIZE;
    //valid?
    if (!info->extra_info[index][val]){
    	return -1;
    }
    //resident?
    //   free in mem? go 
    //       find a victim
    //       	my disk?
    //          	free block?
    //					others' block.
    if (!info->extra_info[index][res]){
    	if (!free_phy_mem_page_list.empty()) {
    		info->page_table.ptes[info->top_virtual_page_num].ppage=free_phy_mem_page_list.back();
			// val=1, res=1, ref=0, dirt=0, new=1, zero=1, disk_num=0
			page_extra_info ei = {1,1,0,0,1,1,0};
			info->extra_info.push_back(ei);

			free_phy_mem_page_list.pop_back();

			virtual_page_indentifier vpi = {current_pid, top_virtual_page_num}
			clock_queue.push_back(vpi);

			return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
			info->top_virtual_page_num += 1;
    	}
    	else {
    		while ()
    	}

    }

}

int 
vm_syslog(void *message, unsigned int len)
{

}
