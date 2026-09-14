/* Dump every online display's transfer table: cgtable <out-file> */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
int main(int argc, char **argv)
{
	CGDirectDisplayID displays[16];
	CGDisplayCount n, i, j;
	FILE *f = argc > 1 ? fopen(argv[1], "w") : stdout;
	if (!f || CGGetOnlineDisplayList(16, displays, &n) != kCGErrorSuccess) return 1;
	for (i = 0; i < n; i++) {
		CGGammaValue r[1024], g[1024], b[1024];
		uint32_t cap = CGDisplayGammaTableCapacity(displays[i]), count = 0;
		if (cap > 1024) cap = 1024;
		if (CGGetDisplayTransferByTable(displays[i], cap, r, g, b, &count) != kCGErrorSuccess) return 2;
		fprintf(f, "display %u main=%d count=%u\n", displays[i], CGDisplayIsMain(displays[i]), count);
		for (j = 0; j < count; j++) fprintf(f, "%.6f %.6f %.6f\n", r[j], g[j], b[j]);
		if (f != stdout) printf("display %u main=%d count=%u mid=%.4f last=%.4f\n", displays[i], CGDisplayIsMain(displays[i]), count, r[count/2], r[count-1]);
	}
	return 0;
}
