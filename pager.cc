#include "vm_pager.h"
#include <map>
#include <stdlib.h>
#include <stdio.h>
#include <vector>
#include <string.h>
#include <list>
#include <iostream>

using namespace std;

/**********
data structure needed:
	free_phy_mem_list: a linked list (vector) for free physical mem page

	free_disk_block_list: a linked list (vector) for free disk block

	proc_vm_info: store vm info of a process
		page_table: page_table_t variable
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
	struct node *clock_node;
} page_extra_info;

typedef struct {
	page_table_t page_table;
	vector<page_extra_info> extra_info;
	int top_virtual_page_num;
} proc_vm_info;

map<pid_t, proc_vm_info *> vm_info;

typedef struct {
	pid_t pid;
	int page_num;
} virtual_page_indentifier;
bool operator< (virtual_page_indentifier a, virtual_page_indentifier b) {
	return a.pid < b.pid;
}

struct node{
	struct node *next;
	struct node *former;
	virtual_page_indentifier vpi;
};

class Clock_queue{
	struct node dummy_head;
	struct node* clock_pointer;
	int ref_lookup(virtual_page_indentifier vpi) {
		return vm_info[vpi.pid]->extra_info[vpi.page_num].ref;
	}
	void ref_revise(virtual_page_indentifier vpi) {
		vm_info[vpi.pid]->extra_info[vpi.page_num].ref = 0;
		vm_info[vpi.pid]->page_table.ptes[vpi.page_num].read_enable = 0;
		vm_info[vpi.pid]->page_table.ptes[vpi.page_num].write_enable = 0;
	}
public:
	Clock_queue(){
		clock_pointer = &dummy_head;
		dummy_head.next = &dummy_head;
		dummy_head.former = &dummy_head;
	}

	void free_node(struct node *node){
		if (node == clock_pointer) {
			clock_pointer = clock_pointer->next;
		}
		node->former->next = node->next;
		node->next->former = node->former;
		free(node);
	}
	struct node *push_back(virtual_page_indentifier vpi) {

		struct node *node = (struct node *)calloc(1, sizeof(struct node));
		node->former = dummy_head.former;
		node->next = &dummy_head;
		node->vpi = vpi;
		dummy_head.former->next = node;
		dummy_head.former = node;
		return node;
	}

	virtual_page_indentifier get_victim(){
		while (1){
			
			// skip dummy head
			if (clock_pointer == &dummy_head) {
				clock_pointer = clock_pointer->next;
				continue;
			}
			
			// if the process has exited free the item
			
			if (vm_info.count(clock_pointer->vpi.pid) == 0) {
			
				clock_pointer->former->next = clock_pointer->next;
				clock_pointer->next->former = clock_pointer->former;
				struct node *to_del = clock_pointer;
				clock_pointer = clock_pointer->next;
				free(to_del);
				continue;
			}
			
			// lookup and set 0 ref bit
			if (!ref_lookup(clock_pointer->vpi)) {
				break;
			}
			ref_revise(clock_pointer->vpi);
			clock_pointer = clock_pointer->next;
		}

		// remove victim out of the queue
		clock_pointer->former->next = clock_pointer->next;
		clock_pointer->next->former = clock_pointer->former;
		virtual_page_indentifier victim = clock_pointer->vpi;
		struct node *to_del = clock_pointer;
		clock_pointer = clock_pointer->next;
		free(to_del);
		return victim;
	}

	void inspect() {
		struct node *node = dummy_head.next;
		while (node != &dummy_head) {
			cout << "page_num: " <<node->vpi.page_num << "ref: " << ref_lookup(node->vpi) <<"ooooo\n";
			node = node->next;
		}
	}
};

Clock_queue *clock_queue;

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
	for (int i = 0; i < memory_pages; i++) {
		free_phy_mem_page_list.push_back(i);
	}
	for (int i = 0; i < disk_blocks; i++) {
		free_disk_block_list.push_back(i);
	}

	clock_queue = new Clock_queue;
}

void 
vm_create(pid_t pid)
{
	proc_vm_info *info = (proc_vm_info *)calloc(1, sizeof(proc_vm_info));
	vm_info[pid] = info;
	
}

void 
vm_switch(pid_t pid)
{
	
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
		virtual_page_indentifier vpi = {current_pid, info->top_virtual_page_num};

		info->page_table.ptes[info->top_virtual_page_num].ppage=free_phy_mem_page_list.back();

		// val=1, res=1, ref=0, dirt=0, new=1, zero=1, disk_num=0
		page_extra_info ei = {1,1,0,0,1,1,0,clock_queue->push_back(vpi)};
		info->extra_info.push_back(ei);

		free_phy_mem_page_list.pop_back();


		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;

	} else if (!free_disk_block_list.empty()) {
		// val=1, res=0, ref=0, dirt=0, new=1, zero=1, disk_num=..back()
		page_extra_info ei = {1,0,0,0,1,1,free_disk_block_list.back(),0};
		info->extra_info.push_back(ei);

		free_disk_block_list.pop_back();

		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;

	} else if (!temp_disk_block_map.empty()) {
		virtual_page_indentifier vpi = temp_disk_block_map.begin()->first;

		// val=1, res=0, ref=0, dirt=0, new=1, zero=1, disk_num=..second
		page_extra_info ei = {1,0,0,0,1,1,temp_disk_block_map.begin()->second,0};
		info->extra_info.push_back(ei);

		temp_disk_block_map.erase(vpi);

		return_addr = (void*)((char*)VM_ARENA_BASEADDR + VM_PAGESIZE*info->top_virtual_page_num);
		info->top_virtual_page_num += 1;

	}
	
	return return_addr;

}
/**********
vm_destroy()
	for each valid page in current process
		if it is resident
			return its ppage to free_list
			delete its clock node
			if it is temped
				delete it in temp-map
				return disk_page to free_list
		if it is non-resident
			return its disk block to free_list
	free its proc_vm_info
***********/
void 
vm_destroy()
{
	proc_vm_info *info = vm_info[current_pid];
	for (int i = 0; i < info->top_virtual_page_num; i++) {
		if (info->extra_info[i].res) {
			free_phy_mem_page_list.push_back(info->page_table.ptes[i].ppage);
			// if in temp?
			virtual_page_indentifier vpi = {current_pid, i};
			clock_queue->free_node(info->extra_info[i].clock_node);
			
			if (temp_disk_block_map.count(vpi)){
				free_disk_block_list.push_back(temp_disk_block_map[vpi]);
				temp_disk_block_map.erase(vpi);
			}
		} else {
			free_disk_block_list.push_back(info->extra_info[i].disk_num);
		}
	}
	free(info);
	vm_info.erase(current_pid);
	
	clock_queue->inspect();
	
}

int 
vm_fault(void *addr, bool write_flag)
{
	unsigned long page_number = ((unsigned long)addr - (unsigned long)VM_ARENA_BASEADDR) / VM_PAGESIZE;
	proc_vm_info *info = vm_info[current_pid];
	virtual_page_indentifier vpi = {current_pid, page_number};
	page_extra_info ei = info->extra_info[page_number];
	/*
	printf("faulting addr is %p, which is page %ld:\n",addr,page_number);
	if (write_flag)
		printf("fault is cause by write\n");
	else
		printf("fault is cause by read\n");
	printf("fault flags are valid:%d,res:%d,ref:%d,dirt:%d,init:%d,zero:%d\n",ei.val ,ei.res,ei.ref,ei.dirt,ei.init,ei.zero);
	*/
	if (!ei.val) {
		return -1;
	}
	if (!ei.res) {
		
		// get a free memory page
		//     there is free mem
		//     find a victim, evict it thus get a free mem 
		// if page-in page's zero bit is set
		//     set init bit 1
		// else
		//     disk read
		//     add disk_num to temp-map
		// write free mem to its pte
		// set res = 1
		unsigned long free_page;
		if (!free_phy_mem_page_list.empty()) {
			free_page = free_phy_mem_page_list.back();
			free_phy_mem_page_list.pop_back();
		} else {
			// set victim's !res !read and !write
			// get victim's phy_mem page
			// if victim is temped 
			//     remove it from temp-map
			// if victim is 0 page
			//     do nothing
			// if there is free disk
			//     wrtie disk and set disk_num of victim
			// else
			//     find a block in temp-map remove it, write disk and set disk num of victim
			
			//clock_queue->inspect();
			virtual_page_indentifier victim = clock_queue->get_victim();
			
			proc_vm_info *victim_info = vm_info[victim.pid];
			victim_info->extra_info[victim.page_num].res = 0;
			victim_info->page_table.ptes[victim.page_num].write_enable = 0;
			victim_info->page_table.ptes[victim.page_num].read_enable = 0;
			free_page = victim_info->page_table.ptes[victim.page_num].ppage;
			
			if (temp_disk_block_map.count(victim)) {
				temp_disk_block_map.erase(victim);

			} else if (victim_info->extra_info[victim.page_num].zero) {

			} else if (!free_disk_block_list.empty()) {
				victim_info->extra_info[victim.page_num].disk_num = free_disk_block_list.back();
				disk_write(free_disk_block_list.back(), free_page);
				free_disk_block_list.pop_back();

			} else {
				victim_info->extra_info[victim.page_num].disk_num = temp_disk_block_map.begin()->second;
				temp_disk_block_map.erase(temp_disk_block_map.begin()->first);
				disk_write(victim_info->extra_info[victim.page_num].disk_num, free_page);
			}

		}

		if (info->extra_info[page_number].zero) {
			info->extra_info[page_number].init = 1;
			free_disk_block_list.push_back(info->extra_info[page_number].disk_num);
		} else {
			temp_disk_block_map[vpi] = info->extra_info[page_number].disk_num;
			disk_read(info->extra_info[page_number].disk_num ,free_page);
		}
		info->page_table.ptes[page_number].ppage = free_page;
		info->extra_info[page_number].clock_node = clock_queue->push_back(vpi);

	}
	ei = info->extra_info[page_number];
	if (write_flag) {
		page_extra_info new_ei = {1,1,1,1,0,0,ei.disk_num,ei.clock_node};
		info->page_table.ptes[page_number].write_enable = 1;
		info->page_table.ptes[page_number].read_enable = 1;

		if (temp_disk_block_map.count(vpi)){
			free_disk_block_list.push_back(temp_disk_block_map[vpi]);
			temp_disk_block_map.erase(vpi);
		}
		if (ei.init) {
			memset((char*)pm_physmem+info->page_table.ptes[page_number].ppage * VM_PAGESIZE,0,VM_PAGESIZE);
		}
		info->extra_info[page_number] = new_ei;

	} else {
		page_extra_info new_ei = {1,1,1,ei.dirt,0,ei.zero,ei.disk_num,ei.clock_node};
		info->page_table.ptes[page_number].read_enable = 1;
		if (ei.init) {
			memset((char*)pm_physmem+info->page_table.ptes[page_number].ppage * VM_PAGESIZE,0,VM_PAGESIZE);
		}
		info->extra_info[page_number] = new_ei;
	}

	return 0;
}
/*
page 

*/
int 
vm_syslog(void *message, unsigned int len)
{
	unsigned long start_page = ((unsigned long)message-(unsigned long)VM_ARENA_BASEADDR)/VM_PAGESIZE;
	unsigned long end_page = ((unsigned long)message+len-(unsigned long)VM_ARENA_BASEADDR - 1)/VM_PAGESIZE;

	if (len == 0)
		return -1;
	proc_vm_info *info = vm_info[current_pid];
	if (info->top_virtual_page_num <= end_page)
		return -1;
	char *result = (char *)calloc(len+1, sizeof(char));
	unsigned long len_cal = 0;
	for (unsigned int i = start_page; i<= end_page; i++){
		// read from addr_start to addr_end;
		// addr_start = max(this page_top, message)
		// addr_end = min(this page_bottom, message + len - 1)
		unsigned long page_sta = (unsigned long)VM_ARENA_BASEADDR + VM_PAGESIZE * i;
		unsigned long page_end = (unsigned long)VM_ARENA_BASEADDR + VM_PAGESIZE * i + VM_PAGESIZE - 1;
		unsigned long sta_offset = 0;
		unsigned long end_offset = VM_PAGESIZE - 1;
		if (i == start_page){
			sta_offset = (unsigned long)message - page_sta;
		}
		if (i == end_page){
			end_offset = (unsigned long)message - page_sta + len - 1;
		}
		unsigned long ppage_addr = info->page_table.ptes[i].ppage * VM_PAGESIZE + (unsigned long)pm_physmem;
		if(info->page_table.ptes[i].read_enable == 0){
			vm_fault((void *)page_sta, 0);
		}
		
		memcpy(result+len_cal, (const void*)(ppage_addr+sta_offset),end_offset - sta_offset + 1);
		len_cal += end_offset - sta_offset + 1;
		
	}
	cout<<"syslog \t\t\t"<<result<<endl;
	free(result);
	return 0;
}
