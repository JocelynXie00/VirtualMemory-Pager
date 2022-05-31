#include "vm_app.h"
#include <map>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

using namespace std;

int main()
{
	printf("Start!\n");
	char *p;

	p = (char *) vm_extend();
	vm_extend();
    p[8190] = 'h';
    p[8191] = 'e';
    p[8192] = 'l';
    p[8193] = 'l';
    p[8194] = 'o';
    p[8195] = '\0';
    vm_syslog(&p[8190], 5);
    cout<<vm_syslog(&p[8190], 5)<<endl;
	return 0;
}