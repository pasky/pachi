#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "debug.h"
#include "pattern3.h"
#include "tactics.h"


static void
pattern_record(char *table, char *str, int pat, int fixed_color)
{
	/* Original color assignment */
	table[pat] = fixed_color ? fixed_color : 3;
	//fprintf(stderr, "[%s] %04x %d\n", str, pat, fixed_color);

	/* Reverse color assignment - achieved by swapping odd and even bits */
	pat = ((pat >> 1) & 0x5555) | ((pat & 0x5555) << 1);
	table[pat] = fixed_color ? 2 - (fixed_color == 2) : 3;
	//fprintf(stderr, "[%s] %04x %d\n", str, pat, fixed_color);
}

static int
pat_vmirror(int pat)
{
	/* V mirror pattern; reverse order of 3-2-3 chunks */
	return ((pat & 0xfc00) >> 10) | (pat & 0x03c0) | ((pat & 0x003f) << 10);
}

static int
pat_hmirror(int pat)
{
	/* H mirror pattern; reverse order of 2-bit values within the chunks */
#define rev3(p) ((p >> 4) | (p & 0xc) | ((p & 0x3) << 4))
#define rev2(p) ((p >> 2) | ((p & 0x3) << 2))
	return (rev3((pat & 0xfc00) >> 10) << 10)
		| (rev2((pat & 0x03c0) >> 6) << 6)
		| rev3((pat & 0x003f));
#undef rev3
#undef rev2
}

static int
pat_90rot(int pat)
{
	/* Rotate by 90 degrees:
	 * 5 6 7    7 4 2
	 * 3   4 -> 6   1
	 * 0 1 2    5 3 0 */
	/* I'm too lazy to optimize this :) */
	int vals[8];
	for (int i = 0; i < 8; i++)
		vals[i] = (pat >> (i * 2)) & 0x3;
	int vals2[8];
	vals2[0] = vals[5]; vals2[1] = vals[3]; vals2[2] = vals[0];
	vals2[3] = vals[6];                     vals2[4] = vals[1];
	vals2[5] = vals[7]; vals2[6] = vals[4]; vals2[7] = vals[2];
	int p2 = 0;
	for (int i = 0; i < 8; i++)
		p2 |= vals2[i] << (i * 2);
	return p2;
}

static void
pattern_gen(struct pattern3s *p, int pat, char *src, int srclen, int fixed_color)
{
	for (; srclen > 0; src++, srclen--) {
		if (srclen == 5)
			continue;
		int patofs = (srclen > 5 ? srclen - 1 : srclen) - 1;
		switch (*src) {
			case '?':
				*src = '.'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = '?'; // for future recursions
				return;
			case 'x':
				*src = '.'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'O'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'x'; // for future recursions
				return;
			case 'o':
				*src = '.'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'X'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = '#'; pattern_gen(p, pat, src, srclen, fixed_color);
				*src = 'o'; // for future recursions
				return;
			case '.': /* 0 */ break;
			case 'X': pat |= S_BLACK << (patofs * 2); break;
			case 'O': pat |= S_WHITE << (patofs * 2); break;
			case '#': pat |= S_OFFBOARD << (patofs * 2); break;
		}
	}

	/* Original pattern, all transpositions and rotations */
	pattern_record(p->hash, src - 9, pat, fixed_color);
	pattern_record(p->hash, src - 9, pat_vmirror(pat), fixed_color);
	pattern_record(p->hash, src - 9, pat_hmirror(pat), fixed_color);
	pattern_record(p->hash, src - 9, pat_vmirror(pat_hmirror(pat)), fixed_color);
	pattern_record(p->hash, src - 9, pat_90rot(pat), fixed_color);
	pattern_record(p->hash, src - 9, pat_90rot(pat_vmirror(pat)), fixed_color);
	pattern_record(p->hash, src - 9, pat_90rot(pat_hmirror(pat)), fixed_color);
	pattern_record(p->hash, src - 9, pat_90rot(pat_vmirror(pat_hmirror(pat))), fixed_color);
}

static void
patterns_gen(struct pattern3s *p, char src[][11], int src_n)
{
	for (int i = 0; i < src_n; i++) {
		//printf("<%s>\n", src[i]);
		int fixed_color = 0;
		switch (src[i][9]) {
			case 'X': fixed_color = S_BLACK; break;
			case 'O': fixed_color = S_WHITE; break;
		}
		//fprintf(stderr, "** %s **\n", src[i]);
		pattern_gen(p, 0, src[i], 9, fixed_color);
	}
}

static bool
patterns_load(char src[][11], int src_n, char *filename)
{
	FILE *f = fopen("moggy.patterns", "r");
	if (!f) return false;

	int i;
	for (i = 0; i < src_n; i++) {
		char line[32];
		if (!fgets(line, sizeof(line), f))
			goto error;
		int l = strlen(line);
		if (l != 10 + (line[l - 1] == '\n'))
			goto error;
		memcpy(src[i], line, 10);
	}
	fprintf(stderr, "moggy.patterns: %d patterns loaded\n", i);
	fclose(f);
	return true;
error:
	fprintf(stderr, "Error loading moggy.patterns.\n");
	fclose(f);
	return false;
}

void
pattern3s_init(struct pattern3s *p, char src[][11], int src_n)
{
	char nsrc[src_n][11];

	if (!patterns_load(nsrc, src_n, "moggy.patterns")) {
		/* Use default pattern set. */
		for (int i = 0; i < src_n; i++)
			strcpy(nsrc[i], src[i]);
	}

	patterns_gen(p, nsrc, src_n);
}
