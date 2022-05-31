
#include <string.h>
#include <iostream>
void f(unsigned int x)
{
	int y = x;
	unsigned long int z;
	z = y;
	printf("%lx", z);
}
using namespace std;
int main()
{
	int x = 0x6275d0;
	int y = 0x627c20;
	printf("%d", y-x);
	

	return 0;
}	