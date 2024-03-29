/*
 * The CMap data structure here is constructed on the fly by
 * adding simple range-to-range mappings. Then the data structure
 * is optimized to contain both range-to-range and range-to-table
 * lookups.
 *
 * Any one-to-many mappings are inserted as one-to-table
 * lookups in the beginning, and are not affected by the optimization
 * stage.
 *
 * There is a special function to add a 256-length range-to-table mapping.
 * The ranges do not have to be added in order.
 *
 * This code can be a lot simpler if we don't care about wasting memory,
 * or can trust the parser to give us optimal mappings.
 */

#include "fitz.h"
#include "mupdf.h"

typedef struct pdf_range_s pdf_range;

enum { MAXCODESPACE = 10 };
enum { SINGLE, RANGE, TABLE, MULTI };

struct pdf_range_s
{
	int low;
	int high;
	int flag;	/* what kind of lookup is this */
	int offset;	/* either range-delta or table-index */
};

static int
cmprange(const void *va, const void *vb)
{
	return ((const pdf_range*)va)->low - ((const pdf_range*)vb)->low;
}

struct pdf_cmap_s
{
	int refs;
	int staticdata;
	char cmapname[32];

	char usecmapname[32];
	pdf_cmap *usecmap;

	int wmode;

	int ncspace;
	struct {
		int n;
		unsigned char lo[4];
		unsigned char hi[4];
	} cspace[MAXCODESPACE];

	int rlen, rcap;
	pdf_range *ranges;

	int tlen, tcap;
	int *table;
};

/*
 * Allocate, destroy and simple parameters.
 */

fz_error *
pdf_newcmap(pdf_cmap **cmapp)
{
	pdf_cmap *cmap;

	cmap = *cmapp = fz_malloc(sizeof(pdf_cmap));
	if (!cmap)
		return fz_throw("outofmem: cmap struct");

	cmap->refs = 1;
	cmap->staticdata = 0;
	strcpy(cmap->cmapname, "");

	strcpy(cmap->usecmapname, "");
	cmap->usecmap = nil;

	cmap->wmode = 0;

	cmap->ncspace = 0;

	cmap->rlen = 0;
	cmap->rcap = 0;
	cmap->ranges = nil;

	cmap->tlen = 0;
	cmap->tcap = 0;
	cmap->table = nil;

	return fz_okay;
}

pdf_cmap *
pdf_keepcmap(pdf_cmap *cmap)
{
	cmap->refs ++;
	return cmap;
}

void
pdf_dropcmap(pdf_cmap *cmap)
{
	if (--cmap->refs == 0)
	{
		if (cmap->usecmap)
			pdf_dropcmap(cmap->usecmap);
		if (!cmap->staticdata)
		{
			fz_free(cmap->ranges);
			fz_free(cmap->table);
		}
		fz_free(cmap);
	}
}

pdf_cmap *
pdf_getusecmap(pdf_cmap *cmap)
{
	return cmap->usecmap;
}

void
pdf_setusecmap(pdf_cmap *cmap, pdf_cmap *usecmap)
{
	int i;

	if (cmap->usecmap)
		pdf_dropcmap(cmap->usecmap);
	cmap->usecmap = pdf_keepcmap(usecmap);

	if (cmap->ncspace == 0)
	{
		cmap->ncspace = usecmap->ncspace;
		for (i = 0; i < usecmap->ncspace; i++)
			cmap->cspace[i] = usecmap->cspace[i];
	}
}

int
pdf_getwmode(pdf_cmap *cmap)
{
	return cmap->wmode;
}

void
pdf_setwmode(pdf_cmap *cmap, int wmode)
{
	cmap->wmode = wmode;
}

void
pdf_debugcmap(pdf_cmap *cmap)
{
	int i, k, n;

	printf("cmap $%p /%s {\n", (void *) cmap, cmap->cmapname);

	if (cmap->usecmapname[0])
		printf("  usecmap /%s\n", cmap->usecmapname);
	if (cmap->usecmap)
		printf("  usecmap $%p\n", (void *) cmap->usecmap);

	printf("  wmode %d\n", cmap->wmode);

	printf("  codespaces {\n");
	for (i = 0; i < cmap->ncspace; i++)
	{
		printf("    <");
		for (k = 0; k < cmap->cspace[i].n; k++)
			printf("%02x", cmap->cspace[i].lo[k]);
		printf("> <");
		for (k = 0; k < cmap->cspace[i].n; k++)
			printf("%02x", cmap->cspace[i].hi[k]);
		printf(">\n");
	}
	printf("  }\n");

	printf("  ranges (%d,%d) {\n", cmap->rlen, cmap->tlen);
	for (i = 0; i < cmap->rlen; i++)
	{
		pdf_range *r = &cmap->ranges[i];
		printf("    <%04x> <%04x> ", r->low, r->high);
		if (r->flag == TABLE)
		{
			printf("[ ");
			for (k = 0; k < r->high - r->low + 1; k++)
				printf("%d ", cmap->table[r->offset + k]);
			printf("]\n");
		}
		else if (r->flag == MULTI)
		{
			printf("< ");
			n = cmap->table[r->offset];
			for (k = 0; k < n; k++)
				printf("%04x ", cmap->table[r->offset + 1 + k]);
			printf(">\n");
		}
		else
			printf("%d\n", r->offset);
	}
	printf("  }\n}\n");
}

/*
 * Add a codespacerange section.
 * These ranges are used by pdf_decodecmap to decode
 * multi-byte encoded strings.
 */
fz_error *
pdf_addcodespace(pdf_cmap *cmap, unsigned lo, unsigned hi, int n)
{
	int i;

	assert(!cmap->staticdata);

	if (cmap->ncspace + 1 == MAXCODESPACE)
		return fz_throw("assert: too many code space ranges");

	cmap->cspace[cmap->ncspace].n = n;

	for (i = 0; i < n; i++)
	{
		int o = (n - i - 1) * 8;
		cmap->cspace[cmap->ncspace].lo[i] = (lo >> o) & 0xFF;
		cmap->cspace[cmap->ncspace].hi[i] = (hi >> o) & 0xFF;
	}

	cmap->ncspace ++;

	return fz_okay;
}

/*
 * Add an integer to the table.
 */
static fz_error *
addtable(pdf_cmap *cmap, int value)
{
	assert(!cmap->staticdata);
	if (cmap->tlen + 1 > cmap->tcap)
	{
		int newcap = cmap->tcap == 0 ? 256 : cmap->tcap * 2;
		int *newtable = fz_realloc(cmap->table, newcap * sizeof(int));
		if (!newtable)
			return fz_throw("outofmem: cmap table");
		cmap->tcap = newcap;
		cmap->table = newtable;
	}

	cmap->table[cmap->tlen++] = value;

	return fz_okay;
}

/*
 * Add a range.
 */
static fz_error *
addrange(pdf_cmap *cmap, int low, int high, int flag, int offset)
{
	assert(!cmap->staticdata);
	if (cmap->rlen + 1 > cmap->rcap)
	{
		pdf_range *newranges;
		int newcap = cmap->rcap == 0 ? 256 : cmap->rcap * 2;
		newranges = fz_realloc(cmap->ranges, newcap * sizeof(pdf_range));
		if (!newranges)
			return fz_throw("outofmem: cmap ranges");
		cmap->rcap = newcap;
		cmap->ranges = newranges;
	}

	cmap->ranges[cmap->rlen].low = low;
	cmap->ranges[cmap->rlen].high = high;
	cmap->ranges[cmap->rlen].flag = flag;
	cmap->ranges[cmap->rlen].offset = offset;
	cmap->rlen ++;

	return fz_okay;
}

/*
 * Add a range-to-table mapping.
 */
fz_error *
pdf_maprangetotable(pdf_cmap *cmap, int low, int *table, int len)
{
	fz_error *error;
	int offset;
	int high;
	int i;

	high = low + len;
	offset = cmap->tlen;

	for (i = 0; i < len; i++)
	{
		error = addtable(cmap, table[i]);
		if (error)
			return fz_rethrow(error, "cannot add range-to-table index");
	}

	error = addrange(cmap, low, high, TABLE, offset);
	if (error)
		return fz_rethrow(error, "cannot add range-to-table range");

	return fz_okay;
}

/*
 * Add a range of contiguous one-to-one mappings (ie 1..5 maps to 21..25)
 */
fz_error *
pdf_maprangetorange(pdf_cmap *cmap, int low, int high, int offset)
{
	fz_error *error;
	error = addrange(cmap, low, high, high - low == 0 ? SINGLE : RANGE, offset);
	if (error)
		return fz_rethrow(error, "cannot add range-to-range mapping");
	return fz_okay;
}

/*
 * Add a single one-to-many mapping.
 */
fz_error *
pdf_maponetomany(pdf_cmap *cmap, int low, int *values, int len)
{
	fz_error *error;
	int offset;
	int i;

	if (len == 1)
	{
		error = addrange(cmap, low, low, SINGLE, values[0]);
		if (error)
			return fz_rethrow(error, "cannot add one-to-one mapping");
		return fz_okay;
	}

	offset = cmap->tlen;

	error = addtable(cmap, len);
	if (error)
		return fz_rethrow(error, "cannot add one-to-many table length");

	for (i = 0; i < len; i++)
	{
		error = addtable(cmap, values[i]);
		if (error)
			return fz_rethrow(error, "cannot add one-to-many table index");
	}

	error = addrange(cmap, low, low, MULTI, offset);
	if (error)
		return fz_rethrow(error, "cannot add one-to-many mapping");

	return fz_okay;
}

/*
 * Sort the input ranges.
 * Merge contiguous input ranges to range-to-range if the output is contiguous.
 * Merge contiguous input ranges to range-to-table if the output is random.
 */
fz_error *
pdf_sortcmap(pdf_cmap *cmap)
{
	fz_error *error;
	pdf_range *newranges;
	int *newtable;
	pdf_range *a;			/* last written range on output */
	pdf_range *b;			/* current range examined on input */

	assert(!cmap->staticdata);

	if (cmap->rlen == 0)
		return fz_okay;

	qsort(cmap->ranges, cmap->rlen, sizeof(pdf_range), cmprange);

	a = cmap->ranges;
	b = cmap->ranges + 1;

	while (b < cmap->ranges + cmap->rlen)
	{
		/* ignore one-to-many mappings */
		if (b->flag == MULTI)
		{
			*(++a) = *b;
		}

		/* input contiguous */
		else if (a->high + 1 == b->low)
		{
			/* output contiguous */
			if (a->high - a->low + a->offset + 1 == b->offset)
			{
				/* SR -> R and SS -> R and RR -> R and RS -> R */
				if (a->flag == SINGLE || a->flag == RANGE)
				{
					a->flag = RANGE;
					a->high = b->high;
				}

				/* LS -> L */
				else if (a->flag == TABLE && b->flag == SINGLE)
				{
					a->high = b->high;
					error = addtable(cmap, b->offset);
					if (error)
						return fz_rethrow(error, "cannot convert LS -> L");
				}

				/* LR -> LR */
				else if (a->flag == TABLE && b->flag == RANGE)
				{
					*(++a) = *b;
				}

				/* XX -> XX */
				else
				{
					*(++a) = *b;
				}
			}

			/* output separated */
			else
			{
				/* SS -> L */
				if (a->flag == SINGLE && b->flag == SINGLE)
				{
					a->flag = TABLE;
					a->high = b->high;

					error = addtable(cmap, a->offset);
					if (error)
						return fz_rethrow(error, "cannot convert SS -> L");

					error = addtable(cmap, b->offset);
					if (error)
						return fz_rethrow(error, "cannot convert SS -> L");

					a->offset = cmap->tlen - 2;
				}

				/* LS -> L */
				else if (a->flag == TABLE && b->flag == SINGLE)
				{
					a->high = b->high;
					error = addtable(cmap, b->offset);
					if (error)
						return fz_rethrow(error, "cannot convert LS -> L");
				}

				/* XX -> XX */
				else
				{
					*(++a) = *b;
				}
			}
		}

		/* input separated: XX -> XX */
		else
		{
			*(++a) = *b;
		}

		b ++;
	}

	cmap->rlen = a - cmap->ranges + 1;

	newranges = fz_realloc(cmap->ranges, cmap->rlen * sizeof(pdf_range));
	if (!newranges)
		return fz_throw("outofmem: cmap ranges");
	cmap->rcap = cmap->rlen;
	cmap->ranges = newranges;

	if (cmap->tlen)
	{
		newtable = fz_realloc(cmap->table, cmap->tlen * sizeof(int));
		if (!newtable)
			return fz_throw("outofmem: cmap table");
		cmap->tcap = cmap->tlen;
		cmap->table = newtable;
	}

	return fz_okay;
}

/*
 * Lookup the mapping of a codepoint.
 */
int
pdf_lookupcmap(pdf_cmap *cmap, int cpt)
{
	int l = 0;
	int r = cmap->rlen - 1;
	int m;

	while (l <= r)
	{
		m = (l + r) >> 1;
		if (cpt < cmap->ranges[m].low)
			r = m - 1;
		else if (cpt > cmap->ranges[m].high)
			l = m + 1;
		else
		{
			int i = cpt - cmap->ranges[m].low + cmap->ranges[m].offset;
			if (cmap->ranges[m].flag == TABLE)
				return cmap->table[i];
			if (cmap->ranges[m].flag == MULTI)
				return -1;
			return i;
		}
	}

	if (cmap->usecmap)
		return pdf_lookupcmap(cmap->usecmap, cpt);

	return -1;
}

/*
 * Use the codespace ranges to extract a codepoint from a
 * multi-byte encoded string.
 */
unsigned char *
pdf_decodecmap(pdf_cmap *cmap, unsigned char *buf, int *cpt)
{
	int i, k;

	for (k = 0; k < cmap->ncspace; k++)
	{
		unsigned char *lo = cmap->cspace[k].lo;
		unsigned char *hi = cmap->cspace[k].hi;
		int n = cmap->cspace[k].n;
		int c = 0;

		for (i = 0; i < n; i++)
		{
			if (lo[i] <= buf[i] && buf[i] <= hi[i])
				c = (c << 8) | buf[i];
			else
				break;
		}

		if (i == n) {
			*cpt = c;
			return buf + n;
		}
	}

	*cpt = 0;
	return buf + 1;
}

/*
 * CMap parser
 */

enum
{
	TUSECMAP = PDF_NTOKENS,
	TBEGINCODESPACERANGE,
	TENDCODESPACERANGE,
	TBEGINBFCHAR,
	TENDBFCHAR,
	TBEGINBFRANGE,
	TENDBFRANGE,
	TBEGINCIDCHAR,
	TENDCIDCHAR,
	TBEGINCIDRANGE,
	TENDCIDRANGE
};

static pdf_token_e tokenfromkeyword(char *key)
{
	if (!strcmp(key, "usecmap")) return TUSECMAP;
	if (!strcmp(key, "begincodespacerange")) return TBEGINCODESPACERANGE;
	if (!strcmp(key, "endcodespacerange")) return TENDCODESPACERANGE;
	if (!strcmp(key, "beginbfchar")) return TBEGINBFCHAR;
	if (!strcmp(key, "endbfchar")) return TENDBFCHAR;
	if (!strcmp(key, "beginbfrange")) return TBEGINBFRANGE;
	if (!strcmp(key, "endbfrange")) return TENDBFRANGE;
	if (!strcmp(key, "begincidchar")) return TBEGINCIDCHAR;
	if (!strcmp(key, "endcidchar")) return TENDCIDCHAR;
	if (!strcmp(key, "begincidrange")) return TBEGINCIDRANGE;
	if (!strcmp(key, "endcidrange")) return TENDCIDRANGE;
	return PDF_TKEYWORD;
}

static int codefromstring(char *buf, int len)
{
	int a = 0;
	while (len--)
		a = (a << 8) | *buf++;
	return a;
}

static fz_error *lexcmap(pdf_token_e *tok, fz_stream *file, char *buf, int n, int *sl)
{
	fz_error *error;
	error = pdf_lex(tok, file, buf, n, sl);
	if (error)
		return fz_rethrow(error, "cannot parse cmap token");

	if (*tok == PDF_TKEYWORD)
		*tok = tokenfromkeyword(buf);

	return fz_okay;
}

static fz_error *parsecmapname(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;

	error = lexcmap(&tok, file, buf, sizeof buf, &len);
	if (error)
		return fz_rethrow(error, "syntaxerror in cmap");

	if (tok == PDF_TNAME)
	{
		strlcpy(cmap->cmapname, buf, sizeof(cmap->cmapname));
		return fz_okay;
	}

	return fz_throw("expected name");
}

static fz_error *parsewmode(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;

	error = lexcmap(&tok, file, buf, sizeof buf, &len);
	if (error)
		return fz_rethrow(error, "syntaxerror in cmap");

	if (tok == PDF_TINT)
	{
		pdf_setwmode(cmap, atoi(buf));
		return fz_okay;
	}

	return fz_throw("expected integer");
}

static fz_error *parsecodespacerange(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int lo, hi;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == TENDCODESPACERANGE)
			return fz_okay;

		else if (tok == PDF_TSTRING)
		{
			lo = codefromstring(buf, len);
			error = lexcmap(&tok, file, buf, sizeof buf, &len);
			if (error)
				return fz_rethrow(error, "syntaxerror in cmap");
			if (tok == PDF_TSTRING)
			{
				hi = codefromstring(buf, len);
				error = pdf_addcodespace(cmap, lo, hi, len);
				if (error)
					return fz_rethrow(error, "cannot add code space");
			}
			else break;
		}

		else break;
	}

	return fz_throw("expected string or endcodespacerange");
}

static fz_error *parsecidrange(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int lo, hi, dst;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == TENDCIDRANGE)
			return fz_okay;

		else if (tok != PDF_TSTRING)
			return fz_throw("expected string or endcidrange");

		lo = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");
		if (tok != PDF_TSTRING)
			return fz_throw("expected string");

		hi = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");
		if (tok != PDF_TINT)
			return fz_throw("expected integer");

		dst = atoi(buf);

		error = pdf_maprangetorange(cmap, lo, hi, dst);
		if (error)
			return fz_rethrow(error, "cannot map cidrange");
	}
}

static fz_error *parsecidchar(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int src, dst;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == TENDCIDCHAR)
			return fz_okay;

		else if (tok != PDF_TSTRING)
			return fz_throw("expected string or endcidchar");

		src = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");
		if (tok != PDF_TINT)
			return fz_throw("expected integer");

		dst = atoi(buf);

		error = pdf_maprangetorange(cmap, src, src, dst);
		if (error)
			return fz_rethrow(error, "cannot map cidchar");
	}
}

static fz_error *parsebfrangearray(pdf_cmap *cmap, fz_stream *file, int lo, int hi)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int dst[256];
	int i;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == PDF_TCARRAY)
			return fz_okay;

		/* Note: does not handle [ /Name /Name ... ] */
		else if (tok != PDF_TSTRING)
			return fz_throw("expected string or ]");

		if (len / 2)
		{
			for (i = 0; i < len / 2; i++)
				dst[i] = codefromstring(buf + i * 2, 2);

			error = pdf_maponetomany(cmap, lo, dst, len / 2);
			if (error)
				return fz_rethrow(error, "cannot map bfrange array");
		}

		lo ++;
	}
}

static fz_error *parsebfrange(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int lo, hi, dst;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == TENDBFRANGE)
			return fz_okay;

		else if (tok != PDF_TSTRING)
			return fz_throw("expected string or endbfrange");

		lo = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");
		if (tok != PDF_TSTRING)
			return fz_throw("expected string");

		hi = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == PDF_TSTRING)
		{
			if (len == 2)
			{
				dst = codefromstring(buf, len);
				error = pdf_maprangetorange(cmap, lo, hi, dst);
				if (error)
					return fz_rethrow(error, "cannot map bfrange");
			}
			else
			{
				int dststr[256];
				int i;

				if (len / 2)
				{
					for (i = 0; i < len / 2; i++)
						dststr[i] = codefromstring(buf + i * 2, 2);

					while (lo <= hi)
					{
						dststr[i-1] ++;
						error = pdf_maponetomany(cmap, lo, dststr, i);
						if (error)
							return fz_rethrow(error, "cannot map bfrange");
						lo ++;
					}
				}
			}
		}

		else if (tok == PDF_TOARRAY)
		{
			error = parsebfrangearray(cmap, file, lo, hi);
			if (error)
				return fz_rethrow(error, "cannot map bfrange");
		}

		else
		{
			return fz_throw("expected string or array or endbfrange");
		}
	}
}

static fz_error *parsebfchar(pdf_cmap *cmap, fz_stream *file)
{
	fz_error *error;
	char buf[256];
	pdf_token_e tok;
	int len;
	int dst[256];
	int src;
	int i;

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");

		if (tok == TENDBFCHAR)
			return fz_okay;

		else if (tok != PDF_TSTRING)
			return fz_throw("expected string or endbfchar");

		src = codefromstring(buf, len);

		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
			return fz_rethrow(error, "syntaxerror in cmap");
		/* Note: does not handle /dstName */
		if (tok != PDF_TSTRING)
			return fz_throw("expected string");

		if (len / 2)
		{
			for (i = 0; i < len / 2; i++)
				dst[i] = codefromstring(buf + i * 2, 2);

			error = pdf_maponetomany(cmap, src, dst, i);
			if (error)
				return fz_rethrow(error, "cannot map bfchar");
		}
	}
}

fz_error *
pdf_parsecmap(pdf_cmap **cmapp, fz_stream *file)
{
	fz_error *error;
	pdf_cmap *cmap;
	char key[64];
	char buf[256];
	pdf_token_e tok;
	int len;

	error = pdf_newcmap(&cmap);
	if (error)
		return fz_rethrow(error, "cannot create cmap");

	strcpy(key, ".notdef");

	while (1)
	{
		error = lexcmap(&tok, file, buf, sizeof buf, &len);
		if (error)
		{
			error = fz_rethrow(error, "syntaxerror in cmap");
			goto cleanup;
		}

		if (tok == PDF_TEOF)
			break;

		else if (tok == PDF_TNAME)
		{
			if (!strcmp(buf, "CMapName"))
			{
				error = parsecmapname(cmap, file);
				if (error)
				{
					error = fz_rethrow(error, "syntaxerror in cmap after /CMapName");
					goto cleanup;
				}
			}
			else if (!strcmp(buf, "WMode"))
			{
				error = parsewmode(cmap, file);
				if (error)
				{
					error = fz_rethrow(error, "syntaxerror in cmap after /WMode");
					goto cleanup;
				}
			}
			else
				strlcpy(key, buf, sizeof key);
		}

		else if (tok == TUSECMAP)
		{
			strlcpy(cmap->usecmapname, key, sizeof(cmap->usecmapname));
		}

		else if (tok == TBEGINCODESPACERANGE)
		{
			error = parsecodespacerange(cmap, file);
			if (error)
			{
				error = fz_rethrow(error, "syntaxerror in cmap codespacerange");
				goto cleanup;
			}
		}

		else if (tok == TBEGINBFCHAR)
		{
			error = parsebfchar(cmap, file);
			if (error)
			{
				error = fz_rethrow(error, "syntaxerror in cmap bfchar");
				goto cleanup;
			}
		}

		else if (tok == TBEGINCIDCHAR)
		{
			error = parsecidchar(cmap, file);
			if (error)
			{
				error = fz_rethrow(error, "syntaxerror in cmap cidchar");
				goto cleanup;
			}
		}

		else if (tok == TBEGINBFRANGE)
		{
			error = parsebfrange(cmap, file);
			if (error)
			{
				error = fz_rethrow(error, "syntaxerror in cmap bfrange");
				goto cleanup;
			}
		}

		else if (tok == TBEGINCIDRANGE)
		{
			error = parsecidrange(cmap, file);
			if (error)
			{
				error = fz_rethrow(error, "syntaxerror in cmap cidrange");
				goto cleanup;
			}
		}

		/* ignore everything else */
	}

	error = pdf_sortcmap(cmap);
	if (error)
	{
		error = fz_rethrow(error, "cannot sort cmap");
		goto cleanup;
	}

	*cmapp = cmap;
	return fz_okay;

cleanup:
	pdf_dropcmap(cmap);
	return error; /* already rethrown */
}

/*
 * Load CMap stream in PDF file
 */
fz_error *
pdf_loadembeddedcmap(pdf_cmap **cmapp, pdf_xref *xref, fz_obj *stmref)
{
	fz_error *error = fz_okay;
	fz_obj *stmobj = stmref;
	fz_stream *file;
	pdf_cmap *cmap = nil;
	pdf_cmap *usecmap;
	fz_obj *wmode;
	fz_obj *obj;

	if ((*cmapp = pdf_finditem(xref->store, PDF_KCMAP, stmref)))
	{
		pdf_keepcmap(*cmapp);
		return fz_okay;
	}

	pdf_logfont("load embedded cmap %d %d {\n", fz_tonum(stmref), fz_togen(stmref));

	error = pdf_resolve(&stmobj, xref);
	if (error)
		return fz_rethrow(error, "cannot resolve cmap object");

	error = pdf_openstream(&file, xref, fz_tonum(stmref), fz_togen(stmref));
	if (error)
	{
		error = fz_rethrow(error, "cannot open cmap stream");
		goto cleanup;
	}

	error = pdf_parsecmap(&cmap, file);
	if (error)
	{
		error = fz_rethrow(error, "cannot parse cmap stream");
		goto cleanup;
	}

	fz_dropstream(file);

	wmode = fz_dictgets(stmobj, "WMode");
	if (fz_isint(wmode))
	{
		pdf_logfont("wmode %d\n", wmode);
		pdf_setwmode(cmap, fz_toint(wmode));
	}

	obj = fz_dictgets(stmobj, "UseCMap");
	if (fz_isname(obj))
	{
		pdf_logfont("usecmap /%s\n", fz_toname(obj));
		error = pdf_loadsystemcmap(&usecmap, fz_toname(obj));
		if (error)
		{
			error = fz_rethrow(error, "cannot load system usecmap '%s'", fz_toname(obj));
			goto cleanup;
		}
		pdf_setusecmap(cmap, usecmap);
		pdf_dropcmap(usecmap);
	}
	else if (fz_isindirect(obj))
	{
		pdf_logfont("usecmap %d %d R\n", fz_tonum(obj), fz_togen(obj));
		error = pdf_loadembeddedcmap(&usecmap, xref, obj);
		if (error)
		{
			error = fz_rethrow(error, "cannot load embedded usecmap");
			goto cleanup;
		}
		pdf_setusecmap(cmap, usecmap);
		pdf_dropcmap(usecmap);
	}

	pdf_logfont("}\n");

	error = pdf_storeitem(xref->store, PDF_KCMAP, stmref, cmap);
	if (error)
	{
		error = fz_rethrow(error, "cannot store cmap resource");
		goto cleanup;
	}

	fz_dropobj(stmobj);

	*cmapp = cmap;
	return fz_okay;

cleanup:
	if (cmap)
		pdf_dropcmap(cmap);
	fz_dropobj(stmobj);
	return error; /* already rethrown */
}

#ifdef WIN32
#define DIR_SEP_STR "\\"
#else
#define DIR_SEP_STR "/"
#endif

#include <ctype.h>

static void filenamesanitze(char *name)
{
	char *tmp = &(name[0]);
	while (*tmp) {
		*tmp = tolower(*tmp);
		if ('-' == *tmp)
			*tmp = '_';
		++tmp;
	}
}

static fz_error*
pdf_dumpcmapasccode(pdf_cmap *cmap, char *name)
{
	char filenamec[256];
	char id[256];
	char idupper[256];
	char *tmp;
	int i, j;
	pdf_range *r;
	int *t;
	fz_stream *file;
	fz_error *error;

	strlcpy(filenamec, name, sizeof filenamec);
	strlcat(filenamec, ".c", sizeof filenamec);
	filenamesanitze(filenamec);

	strlcpy(id, name, sizeof id);
	filenamesanitze(id);

	strlcpy(idupper, name, sizeof idupper);
	tmp = &(idupper[0]);
	while (*tmp) {
		*tmp = toupper(*tmp);
		if ('-' == *tmp)
			*tmp = '_';
		++tmp;
	}

	error = fz_openwfile(&file, filenamec);
	if (error)
	{
		return fz_rethrow(error, "cannot open file '%s'", filenamec);
	}

	fz_print(file, "#ifdef USE_%s\n", idupper);
	fz_print(file, "\n");

	fz_print(file, "#ifdef INCLUDE_CMAP_DATA\n");
	fz_print(file, "\n");

	/* generate table data */
	t = cmap->table;
	if (t && cmap->tlen) {
		fz_print(file, "static const int g_cmap_%s_table[%d] = {\n", id, cmap->tlen);
		for (i = 0; i < cmap->tlen-1; i++) {
			fz_print(file, " %d, ", *t++);
			if (0 == ((i + 1) % 8))
				fz_print(file, "\n");
		}
		fz_print(file, " %d };\n\n", *t++);
	}

	/* generate ranges data */
	r = cmap->ranges;
	if (r && cmap->rlen) {
		fz_print(file, "static const pdf_range g_cmap_%s_ranges[%d] = {\n", id, cmap->rlen);
		for (i = 0; i < cmap->rlen-1; i++) {
			fz_print(file, " {%d, %d, %d, %d},\n", r->low, r->high, r->flag, r->offset);
			++r;
		}
		fz_print(file, " {%d, %d, %d, %d}\n};\n", r->low, r->high, r->flag, r->offset);
	}
	fz_print(file, "\n");

	/* generate new function */
	fz_print(file, "static fz_error *new_%s(pdf_cmap **out)\n", id);
	fz_print(file, "{\n");
	fz_print(file, "\tfz_error *error;\n");
	fz_print(file, "\tpdf_cmap *cmap;\n");
	fz_print(file, "\terror = pdf_newcmap(&cmap);\n");
	fz_print(file, "\tif (error)\n");
	fz_print(file, "\t\treturn error;\n");
	fz_print(file, "\tcmap->staticdata = 1;\n");
	if (r && cmap->rlen)
		fz_print(file, "\tcmap->ranges = (pdf_range*)&g_cmap_%s_ranges[0];\n", id);
	else
		fz_print(file, "\tcmap->ranges = 0;\n");
	if (t && cmap->tlen)
		fz_print(file, "\tcmap->table = (int*)&g_cmap_%s_table[0];\n", id);
	else
		fz_print(file, "\tcmap->table = 0;\n");
	fz_print(file, "\tstrcpy(cmap->cmapname, \"%s\");\n", cmap->cmapname);
	fz_print(file, "\tstrcpy(cmap->usecmapname, \"%s\");\n", cmap->usecmapname);
	fz_print(file, "\tcmap->wmode = %d;\n", cmap->wmode);
	fz_print(file, "\tcmap->ncspace = %d;\n", cmap->ncspace);
	for (i = 0; i < cmap->ncspace; i++)
	{
		fz_print(file, "\tcmap->cspace[%d].n = %d;\n", i, cmap->cspace[i].n);
		for (j = 0; j < 4; j++)
		{
			fz_print(file, "\tcmap->cspace[%d].lo[%d] = %d;\n", i, j, (int)cmap->cspace[i].lo[j]);
			fz_print(file, "\tcmap->cspace[%d].hi[%d] = %d;\n", i, j, (int)cmap->cspace[i].hi[j]);
		}
	}
	fz_print(file, "\t\n");
	fz_print(file, "\tcmap->rlen = %d;\n", cmap->rlen);
	fz_print(file, "\tcmap->rcap = %d;\n", cmap->rcap);
	fz_print(file, "\tcmap->tlen = %d;\n", cmap->tlen);
	fz_print(file, "\tcmap->tcap = %d;\n", cmap->tcap);
	fz_print(file, "\t*out = cmap;\n");
	fz_print(file, "\n");
	fz_print(file, "\treturn fz_okay;\n");
	fz_print(file, "}\n");

	fz_print(file, "\n");

	/* generate part that constructs this cmap if name matches */
	fz_print(file, "#else\n");
	fz_print(file, "\n");
	fz_print(file, "\tif (!strcmp(name, \"%s\"))\n", name);
	fz_print(file, "\t\treturn new_%s(cmapp);\n", id);
	fz_print(file, "\n");
	fz_print(file, "#endif\n");
	fz_print(file, "#endif\n");
	fz_dropstream(file);
	return fz_okay;
}

#ifdef USE_STATIC_CMAPS
#define USE_ADOBE_JAPAN1_UCS2
#define USE_90MSP_RKSJ_H
#define USE_GBK_EUC_H
#define USE_ADOBE_GB1_UCS2
#define USE_ADOBE_CNS1_UCS2
#define USE_ADOBE_KOREA1_UCS2
#define USE_ETEN_B5_H
#define USE_ETENMS_B5_H
#define USE_KSCMS_UHC_H
#define USE_UNIJIS_UCS2_H
#define USE_90MS_RKSJ_H

#define INCLUDE_CMAP_DATA
#include "adobe_japan1_ucs2.c"
#include "90msp_rksj_h.c"
#include "adobe_gb1_ucs2.c"
#include "gbk_euc_h.c"
#include "adobe_cns1_ucs2.c"
#include "adobe_korea1_ucs2.c"
#include "eten_b5_h.c"
#include "etenms_b5_h.c"
#include "kscms_uhc_h.c"
#include "unijis_ucs2_h.c"
#include "90ms_rksj_h.c"

static fz_error *getstaticcmap(char *name, pdf_cmap **cmapp)
{
#undef INCLUDE_CMAP_DATA
#include "adobe_japan1_ucs2.c"
#include "90msp_rksj_h.c"
#include "adobe_gb1_ucs2.c"
#include "gbk_euc_h.c"
#include "adobe_cns1_ucs2.c"
#include "adobe_korea1_ucs2.c"
#include "eten_b5_h.c"
#include "etenms_b5_h.c"
#include "kscms_uhc_h.c"
#include "unijis_ucs2_h.c"
#include "90ms_rksj_h.c"
	return fz_okay;
}
#else
static fz_error *getstaticcmap(char *name, pdf_cmap **cmapp)
{
	return fz_okay;
}
#endif

/*
 * Load predefined CMap from system
 */
fz_error *
pdf_loadsystemcmap(pdf_cmap **cmapp, char *name)
{
	fz_error *error = fz_okay;
	fz_stream *file;
	char *cmapdir;
	char *usecmapname;
	pdf_cmap *usecmap;
	pdf_cmap *cmap;
	char path[1024];

	cmap = nil;
	file = nil;

	pdf_logfont("load system cmap %s {\n", name);
	error = getstaticcmap(name, &cmap);
	if (!error && cmap) {
		*cmapp = cmap;
		return fz_okay;
	}
	if (error)
		fz_droperror(error);

#ifdef DUMP_STATIC_CMAPS
	printf("\nCMAP: filenamec='%s'\n", name);
#endif

	cmapdir = getenv("CMAPDIR");
	if (!cmapdir)
		return fz_throw("ioerror: CMAPDIR environment not set");

	strlcpy(path, cmapdir, sizeof path);
	strlcat(path, DIR_SEP_STR, sizeof path);
	strlcat(path, name, sizeof path);

	error = fz_openrfile(&file, path);
	if (error)
	{
		error = fz_rethrow(error, "cannot open cmap file '%s'", name);
		goto cleanup;
	}

	error = pdf_parsecmap(&cmap, file);
	if (error)
	{
		error = fz_rethrow(error, "cannot parse cmap file");
		goto cleanup;
	}

	fz_dropstream(file);

#ifdef DUMP_STATIC_CMAPS
	pdf_dumpcmapasccode(cmap, name);
#endif

	usecmapname = cmap->usecmapname;
	if (usecmapname[0])
	{
		pdf_logfont("usecmap %s\n", usecmapname);
		error = pdf_loadsystemcmap(&usecmap, usecmapname);
		if (error)
		{
			error = fz_rethrow(error, "cannot load system usecmap '%s'", usecmapname);
			goto cleanup;
		}
		pdf_setusecmap(cmap, usecmap);
		pdf_dropcmap(usecmap);
	}

	pdf_logfont("}\n");

	*cmapp = cmap;
	return fz_okay;

cleanup:
	if (cmap)
		pdf_dropcmap(cmap);
	if (file)
		fz_dropstream(file);
	return error; /* already rethrown */
}

/*
 * Create an Identity-* CMap (for both 1 and 2-byte encodings)
 */
fz_error *
pdf_newidentitycmap(pdf_cmap **cmapp, int wmode, int bytes)
{
	fz_error *error;
	pdf_cmap *cmap;

	error = pdf_newcmap(&cmap);
	if (error)
		return fz_rethrow(error, "cannot create cmap");

	sprintf(cmap->cmapname, "Identity-%c", wmode ? 'V' : 'H');

	error = pdf_addcodespace(cmap, 0x0000, 0xffff, bytes);
	if (error) {
		pdf_dropcmap(cmap);
		return fz_rethrow(error, "cannot add code space");
	}

	error = pdf_maprangetorange(cmap, 0x0000, 0xffff, 0);
	if (error) {
		pdf_dropcmap(cmap);
		return fz_rethrow(error, "cannot map <0000> to <ffff>");
	}

	error = pdf_sortcmap(cmap);
	if (error) {
		pdf_dropcmap(cmap);
		return fz_rethrow(error, "cannot sort cmap");
	}

	pdf_setwmode(cmap, wmode);

	*cmapp = cmap;
	return fz_okay;
}

