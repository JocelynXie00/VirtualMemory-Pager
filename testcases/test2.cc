#include "vm_app.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <iostream>
#include <assert.h>

using namespace std;

int main(int argc, char const *argv[])
{
	char *p[10];
	for (int i = 0; i < 10; i++) {
		p[i] = (char *) vm_extend();
	}
	assert(p[4][4]==0);
	p[5][4] = 'a';
	assert(p[5][4]=='a' && p[5][5]==0);
	assert(p[1][100]==0);
	p[3][100] = 'b';
	assert(p[3][100]=='b' && p[3][0]==0);
	
	p[0][100] = 'c';
	
	p[3][100] = 'd';
	
	p[5][4] = 'e';
	p[4][4] = 'f';
	
	return 0;
}