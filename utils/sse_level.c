#include <stdio.h>
#include <utils.h>
#include <cpuid.h>

int
main(void)
{
	processor_info_t pc;
	cpuid_basic_identify(&pc);
	if (pc.sse_level == 3 && pc.sse_sub_level == 1) {
		printf("ssse%d", pc.sse_level);
		pc.sse_sub_level = 0;
	} else {
		printf("sse%d", pc.sse_level);
	}
	if (pc.sse_sub_level > 0)
		printf(".%d\n", pc.sse_sub_level);
	else
		printf("\n");
	return (0);
}

