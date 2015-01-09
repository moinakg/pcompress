/*
 * This file is a part of Pcompress, a chunked parallel multi-
 * algorithm lossless compression and decompression program.
 *
 * Copyright (C) 2012-2013 Moinak Ghosh. All rights reserved.
 * Use is subject to license terms.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 *
 * moinakg@gmail.com, http://moinakg.wordpress.com/
 */

/*
 * Dictionary preprocessor for text files. It uses some ideas from
 * the following paper:
 * http://pskibinski.pl/papers/05-RevisitingDictCompr.pdf
 *
 * However the implementation here is quite different from that
 * described in the paper. A simple hash table is used for the
 * word dictionary. A min-LRU based aging mechanism is used to evict
 * words with low frequency to make way for newer words. The min-LRU
 * aging kicks in after at least 50% of the data is processed and the
 * hash table is full. The hash table size is derived from the data
 * size.
 * After scanning the data, words with occurrence X word size less
 * than a threshold are evicted from the final dictionary. The
 * dictionary is then prefixed to the encoded data. The words in the
 * final dictionary are sorted based on occurrence X word size value
 * and then alphabetically.
 *
 * Words are extracted by splitting text on a few separator characters.
 * Proper case capital conversion is done. So the dictionary only
 * contains lower case words.
 * Words in the data are replaced by dictionary indexes. These numbers
 * are encoded into a base-217 string. A bunch of non-separator char
 * ranges are used. Each encoded word is prefixed with a backtick (`).
 * Capital converted words are prefixed with an exclamation (!).
 * Apart from encoding words, literal numbers more than 3 digits are
 * replaced with their base-217 encoded strings. These encoded
 * numbers are prefixed with a dollar ($).
 * Since words are only encoded on a separator boundary, any lieral
 * prefix characters following a separator boundary are escaped using
 * a back-slash (\).
 *
 * The separators are prefix characters have been exprimentally
 * selected to benefit context based compressors like PPM and Libbsc.
 * Libbsc is especially finicky about the nature of the transform.
 * For example XWrt (http://xwrt.sourceforge.net/), a preprocessor
 * that implements all of the ideas described in the paper does not
 * benefit Libbsc in the enwik9 test(http://mattmahoney.net/dc/text.html).
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include "DictFilter.h"
#include "utils.h"
#include "allocator.h"
#include "xxhash.h"

#define	WORD_MIN	3
#define	WORD_MAX	50
#define	LIST_LRU_NUM	15

typedef struct dict_entry {
	unsigned char *word;
	unsigned char sz;
	unsigned char lcfirst;
	uint32_t indx;
	uint32_t occur;
	struct dict_entry *next;
	struct dict_entry *list_next;
} dict_entry_t;

typedef struct hash_context_s {
	dict_entry_t    **dict;
	uint32_t        dictcount;
	uint32_t        dictsize;
	uint32_t        cur_indx;
	uint32_t        collisions;
	dict_entry_t    *sentinel;
} hash_context_t;

typedef struct list_context_s {
	dict_entry_t    *head;
	dict_entry_t    *tail;
	uint32_t        listcount;
	uint32_t        listsize;
	uint32_t        aged_entries;
	uint32_t        aging_requests;
} list_context_t;

typedef struct decode_dict_entry_s {
	uint32_t	sz;
	uint8_t		*word;
} decode_dict_entry_t;

/*
 * We are always copying small blocks, typically words, ranging
 * from 3 bytes to 20 bytes. So an inline memory copy is more
 * efficient than memcpy() library calls.
 */
static inline void
copy_bytes(void *dst, void *src, size_t len)
{
	static void *targets[] = { &&zero, &&one, &&two, &&three };

	uint8_t *to = (uint8_t *)dst;
	uint8_t *from = (uint8_t *)src;

	while (len >= sizeof (uint32_t)) {
		*(uint32_t *)to = *(const uint32_t *)from;
		to += sizeof (uint32_t);
		from += sizeof (uint32_t);
		len -= sizeof (uint32_t);
	}

	/* Unroll final small loop using computed goto. */
	goto *targets[len];
three:
	*to = *from;
	to++; from++;
two:
	*to = *from;
	to++; from++;
one:
	*to = *from;
zero:
	return;
}

/*
 * Local replacement for bcmp() avoiding a library call for comparing
 * words.
 */
static inline int
eq_bytes(void *a, void *b, size_t len)
{
	static void *targets[] = { &&_zero, &&_one, &&_two, &&_three };
	uint8_t *to = (uint8_t *)a;
	uint8_t *from = (uint8_t *)b;

	while (len >= sizeof (uint32_t)) {
		if (*(uint32_t *)to != *(uint32_t *)from)
			return (1);
		to += sizeof (uint32_t);
		from += sizeof (uint32_t);
		len -= sizeof (uint32_t);
	}

	/* Unroll final small loop using computed goto. */
	goto *targets[len];
_three:
	if (*to != *from) return (1);
	to++; from++;
_two:
	if (*to != *from) return (1);
	to++; from++;
_one:
	if (*to != *from) return (1);
_zero:
	return (0);
}

/*
 * Sort comparison for the dictionary words.
 * Compare first by occurrence X word length and then alphabetically
 * by the first three letters. Words are at least 3 chars in length.
 */
static int
cmpoccur(const void *a, const void *b) {
	dict_entry_t *de1 = *((dict_entry_t **)a);
	dict_entry_t *de2 = *((dict_entry_t **)b);
	uint64_t a1, b1;

	a1 = ((uint64_t)(de1->occur) - 1) * (de1->sz - 1);
	b1 = ((uint64_t)(de2->occur) - 1) * (de2->sz - 1);

	if (a1 < b1) {
		return (1);
	} else if (a1 == b1) {
		if (de1->sz < de2->sz) {
			return (1);
		} else if (de1->sz == de2->sz) {
			if (de1->word[0] != de2->word[0])
				return ((int)de2->word[0] - (int)de1->word[0]);
			if (de1->word[1] != de2->word[1])
				return ((int)de2->word[1] - (int)de1->word[1]);
			if (de1->word[2] != de2->word[2])
				return ((int)de2->word[2] - (int)de1->word[2]);
			return (0);
		} else {
			return (-1);
		}
	} else {
		return (-1);
	}
}

/*
 * Singleton filter class.
 */
class DictFilter
{
public:
	int Forward_Dict(uint8_t *src, uint32_t size, uint8_t *dst, uint32_t *dstsize);
	int Inverse_Dict(uint8_t *src, uint32_t size, uint8_t *dst, uint32_t *dstsize);

	static DictFilter *getInstance() {
		pthread_mutex_lock(&inst_lock);
		if (!inst) {
			inst = new DictFilter();
		}
		pthread_mutex_unlock(&inst_lock);
		return (inst);
	}

protected:
	~DictFilter();
	DictFilter();

	dict_entry_t *find_string(dict_entry_t *de, uint8_t *str, uint32_t sz,
	    uint8_t lcfirst);
	void hash_context_init(hash_context_t *hctx, uint32_t dictsize);
	void hash_context_delete(hash_context_t *hctx);
	dict_entry_t *hash_lookup(hash_context_t *hctx, uint8_t *word, uint32_t wordsize,
	    uint8_t lcfirst);
	dict_entry_t *hash_add(hash_context_t *hctx, uint8_t *word, uint32_t wordsize,
	    dict_entry_t *_de);
	dict_entry_t *hash_remove(hash_context_t *hctx, uint8_t *word, uint32_t wordsize,
	    dict_entry_t *r_de);

	void list_context_init(list_context_t *lctx, uint32_t listsize);
	void list_context_delete(list_context_t *lctx);
	dict_entry_t *list_push(list_context_t *lctx, dict_entry_t *de);
	dict_entry_t *list_pop_lru_min(list_context_t *lctx);

	uint8_t *to_base_enc(uint32_t number, uint8_t *str, int sz);
	uint32_t from_base_enc(uint8_t *dnum, int sz);

	static pthread_mutex_t inst_lock;
	static DictFilter *inst;
	static const char *BASE_DIGITS;

	uint8_t  SEPARATOR[256], flag, flag1, flag2;
	uint8_t  base_enc_digits[256];
	uint8_t  base_dec_digits[256];
	uint32_t NUMERAL_BASE;
};

pthread_mutex_t DictFilter::inst_lock = PTHREAD_MUTEX_INITIALIZER;
DictFilter *DictFilter::inst = NULL;
const char *DictFilter::BASE_DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz@ABCDEFGHIJKLMNOPQRSTUVWXYZ";

DictFilter::DictFilter()
{
	uint32_t new_size, i;

	memset(SEPARATOR, 0, 256);

	/*
	 * Initialize the number encoding characters. Total
	 * 217 chars are used for a base-217 encoding. In
	 * particular, separator characters are avoided.
	 */
	new_size = strlen(BASE_DIGITS);
	for (i=0; i<new_size; i++) {
		base_dec_digits[(uint8_t)BASE_DIGITS[i]] = i;
		base_enc_digits[i] = BASE_DIGITS[i];
	}

	new_size = 1;
	while (new_size < 9) {
		base_dec_digits[new_size] = i;
		base_enc_digits[i++] = new_size++;
	}

	new_size = 14;
	while (new_size < 32) {
		base_dec_digits[new_size] = i;
		base_enc_digits[i++] = new_size++;
	}

	new_size = 128;
	while (new_size < 256) {
		base_dec_digits[new_size] = i;
		base_enc_digits[i++] = new_size++;
	}

	base_enc_digits[i] = '\0';
	NUMERAL_BASE = i;

	/*
	 * The characters that are regarded as word separators. These
	 * separators are good for general roman alphabet text and
	 * XML/HTML markup text.
	 */
	SEPARATOR['<'] = 1;
	SEPARATOR['['] = 1;
	SEPARATOR['"'] = 1;
	SEPARATOR['('] = 1;
	SEPARATOR['|'] = 1;
	SEPARATOR['/'] = 1;
	SEPARATOR[' '] = 1;
	SEPARATOR['\t'] = 1;
	SEPARATOR[':'] = 1;
	SEPARATOR['\n'] = 1;
	SEPARATOR['\r'] = 1;
	SEPARATOR['>'] = 1;
	SEPARATOR[']'] = 1;
	SEPARATOR['\''] = 1;
	SEPARATOR[')'] = 1;
	SEPARATOR['.'] = 1;
	SEPARATOR['?'] = 1;
	SEPARATOR[','] = 1;
	SEPARATOR[';'] = 1;
	SEPARATOR['='] = 1;
	SEPARATOR['{'] = 1;
	SEPARATOR['}'] = 1;
	SEPARATOR['-'] = 1;
	SEPARATOR['+'] = 1;
	SEPARATOR['*'] = 1;

	/*
	 * Prefix characters for encoded words and numbers.
	 */
	flag = '`';
	flag1 = '!';
	flag2 = '$';

	/*slab_cache_add(sizeof (dict_entry_t));
	slab_cache_add(sizeof (hash_context_t));
	slab_cache_add(sizeof (list_context_t));*/
}

DictFilter::~DictFilter()
{
	pthread_mutex_lock(&inst_lock);
	if (inst) {
		delete inst;
	}
	pthread_mutex_unlock(&inst_lock);
}

uint8_t *
DictFilter::to_base_enc(uint32_t number, uint8_t *str, int sz)
{
	sz--;
	str[sz] = '\0';
	sz--;
	while (number > 0 && sz >= 0) {
		uint32_t rem = number % NUMERAL_BASE;
		str[sz--] = base_enc_digits[rem];
		number /= NUMERAL_BASE;
	}
	sz++;
	return (&str[sz]);
}

uint32_t
DictFilter::from_base_enc(uint8_t *dnum, int sz)
{
	uint32_t pow = 1;
	uint32_t num = 0;

	if (sz == 0) return (0);
	while (sz > 0) {
		uint32_t c = dnum[sz-1];
		c = base_dec_digits[c];
		num += (c * pow);
		pow *= NUMERAL_BASE;
		sz--;
	}
	return (num);
}

/*
 * Search for a string in the hash table bucket chain. The first letter is
 * always lower-cased for Proper-case capital-converted comparison.
 */
dict_entry_t *
DictFilter::find_string(dict_entry_t *de, uint8_t *str, unsigned int sz, uint8_t lcfirst)
{
	uint8_t c1 = lcfirst;
	while(de) {
		if (de->sz == sz) {
			uint8_t c2 = de->lcfirst;
			if (c1 == c2) {
				if (eq_bytes(de->word+1, str+1, sz-1) == 0)
					return (de);
			}
		}
		de = de->next;
	}
	return (NULL);
}

void
DictFilter::hash_context_init(hash_context_t *hctx, uint32_t dictsize)
{
	hctx->dict = new dict_entry_t* [dictsize]();
	hctx->dictcount = 0;
	hctx->dictsize = dictsize;
	hctx->collisions = 0;
	hctx->sentinel = new dict_entry_t[1]();
}

void
DictFilter::hash_context_delete(hash_context_t *hctx) {
	uint32_t i;

	for (i=0; i<hctx->dictsize; i++) {
		if (hctx->dict[i]) {
			dict_entry_t *de, *de1;

			de = hctx->dict[i];
			while (de) {
				de1 = de->next;
				delete de;
				de = de1;
			}
		}
	}
	delete hctx->dict;
	delete hctx->sentinel;
	hctx->dictcount = 0;
	hctx->collisions = 0;
}

dict_entry_t *
DictFilter::hash_lookup(hash_context_t *hctx, uint8_t *word, uint32_t wordsize, uint8_t lcfirst)
{
	uint32_t indx;

	indx = XXH32(word+1, wordsize-1, lcfirst) % hctx->dictsize;
	hctx->cur_indx = indx;
	return (find_string(hctx->dict[indx], word, wordsize, lcfirst));
}

dict_entry_t *
DictFilter::hash_add(hash_context_t *hctx, uint8_t *word, uint32_t wordsize, dict_entry_t *_de)
{
	dict_entry_t *de;
	uint8_t lcfirst;

	lcfirst = tolower(word[0]);

	/*
	 * As of now non-NULL _de means a lookup was already done and match was not found
	 * and the hash table is full.
	 * So we are adding a new entry with a aged out node. No need to do another lookup.
	 */
	if (!_de) {
		de = hash_lookup(hctx, word, wordsize, lcfirst);
		if (de) {
			de->occur++;
			return (hctx->sentinel);
		}

		if (hctx->dictcount == hctx->dictsize)
			return (NULL);
	} else {
		hctx->cur_indx = XXH32(word+1, wordsize-1, lcfirst) % hctx->dictsize;
	}

	if (_de)
		de = _de;
	else
		de = new dict_entry_t[1]();
	de->word = word;
	de->sz = wordsize;
	de->lcfirst = lcfirst;
	de->occur = 1;
	de->indx = hctx->cur_indx;
	if (hctx->dict[hctx->cur_indx])
		hctx->collisions++;

	de->next = hctx->dict[hctx->cur_indx];
	hctx->dict[hctx->cur_indx] = de;
	hctx->dictcount++;
	return (de);
}

dict_entry_t *
DictFilter::hash_remove(hash_context_t *hctx, uint8_t *word, uint32_t wordsize, dict_entry_t *r_de)
{
	dict_entry_t *de;
	uint8_t lcfirst;

	if (!r_de) {
		lcfirst = tolower(word[0]);
		de = hash_lookup(hctx, word, wordsize, lcfirst);
	} else {
		de = r_de;
		hctx->cur_indx = de->indx;
	}
	if (de) {
		dict_entry_t *c_de, *p_de;
		de->indx = UINT32_MAX;

		c_de = hctx->dict[hctx->cur_indx];
		if (c_de == de) {
			hctx->dict[hctx->cur_indx] = c_de->next;
			hctx->dictcount--;
			return (de);
		}

		p_de = c_de;
		c_de = c_de->next;
		while (c_de) {
			if (c_de == de) {
				p_de->next = c_de->next;
				hctx->dictcount--;
				return (de);
			}
			p_de = c_de;
			c_de = c_de->next;
		}
		assert(0 == 1); // Fail, corrupted hash
	}
	return (NULL);
}

void
DictFilter::list_context_init(list_context_t *lctx, uint32_t listsize)
{
	lctx->head = new dict_entry_t[1]();
	lctx->tail = lctx->head;
	lctx->listcount = 0;
	lctx->listsize = listsize;
	lctx->aged_entries = 0;
}

void
DictFilter::list_context_delete(list_context_t *lctx)
{
	delete lctx->head;
	lctx->listcount = 0;
	lctx->aged_entries = 0;
}

dict_entry_t *
DictFilter::list_push(list_context_t *lctx, dict_entry_t *de)
{
	if (lctx->listcount == lctx->listsize)
		return (NULL);

	lctx->tail->list_next = de;
	de->list_next = NULL;
	lctx->tail = de;
	lctx->listcount++;

	return (de);
}

/*
 * Identify a dictionary entry to evict from the N least recently used
 * entries at the list head. The entry with the lowest occurrence count
 * which is below a given threshold is evicted.
 * If no such entry can be found then the current lru aging request is not
 * fulfilled. Also, all the N entries are rotated to the tail of the list.
 * This increases the likelihood of finding an entry to evict for the next
 * request. This allows incremental sequential probing of the list without
 * incurring the cost of very large sequential scans, but at the cost of
 * missing some interesting words.
 * N is kept a small positive number.
 */
dict_entry_t *
DictFilter::list_pop_lru_min(list_context_t *lctx)
{
	dict_entry_t *p_de, *c_de;
	dict_entry_t *min, *min_p;
	uint32_t list_scan, occur, maxoccur;

	if (lctx->listcount == 0)
		return (NULL);

	lctx->aging_requests++;
	p_de = lctx->head;
	c_de = lctx->head->list_next;
	min = NULL;

	if (lctx->listcount > LIST_LRU_NUM)
		list_scan = LIST_LRU_NUM;
	else
		list_scan = lctx->listcount;

	occur = UINT32_MAX;
	maxoccur = 0;
	while (c_de && c_de != lctx->tail && list_scan > 0) {
		if (c_de->occur < occur) {
			min = c_de;
			min_p = p_de;
			occur = c_de->occur;
		}
		list_scan--;
		p_de = c_de;
		c_de = c_de->list_next;
	}

	if (min && min->occur * min->sz < 2048) {
		min_p->list_next = min->list_next;
		lctx->aged_entries++;
		lctx->listcount--;
		return (min);
	}

	if (lctx->listcount > LIST_LRU_NUM) {
		lctx->tail->list_next = lctx->head->list_next;
		lctx->head->list_next = c_de;
		p_de->list_next = NULL;
	}
	return (NULL);
}

int
DictFilter::Forward_Dict(uint8_t *src, uint32_t size, uint8_t *dst, uint32_t *dstsize)
{
	uint32_t dstSize = 0, dictSize, i, pos, num_entries;
	hash_context_t hctx;
	list_context_t lctx;
	dict_entry_t **sorted_dict;
	uint8_t num_dict[10], *numd;
	ssize_t new_size;
	int rv, sz;

	if (size < 1024)
		return 0;

	if (size > 20000) {
		dictSize = size / 10000;
		dictSize += (dictSize >> 1);
	} else {
		dictSize = (size >> 1);
	}
	dictSize++;

	pos = 0;
	rv = 0;
	hash_context_init(&hctx, dictSize);
	list_context_init(&lctx, dictSize);
	sorted_dict = new dict_entry_t* [dictSize];

	/*
	 * Scan words in the data and build the dictionary.
	 */
	for (i=0; i<size; i++) {
		uint8_t c = src[i];

		if (SEPARATOR[c]) {
			dict_entry_t *de;
			size_t toklen = i - pos;

			if (toklen < WORD_MIN || toklen > WORD_MAX) {
				pos = i+1;
				continue;
			}

			de = hash_add(&hctx, src+pos, toklen, NULL);
			if (!de && i > (size>>1)) {
				de = list_pop_lru_min(&lctx);
				if (de) {
					dict_entry_t *de1;
					de1 = hash_remove(&hctx, de->word, de->sz, de);
					assert(de1 == de);
					de1 = hash_add(&hctx, src+pos, toklen, de);
					assert(de1 != NULL);
					assert(de1 != hctx.sentinel);
					list_push(&lctx, de1);
				}
			} else if (de != hctx.sentinel) {
				list_push(&lctx, de);
			}
			pos = i+1;
		}
	}

	/*
	 * Mark below-threshold entries in the dictionary. Also sorted_dict holds a
	 * flattened view of the hash.
	 */
	pos = 0;
	for (i=0; i<dictSize; i++) {
		if (hctx.dict[i]) {
			dict_entry_t *de;

			de = hctx.dict[i];
			while (de) {
				ssize_t val;

				val = (size_t)de->occur * (size_t)de->sz;
				if (val <= 4500) {
					de->occur = 0;
					de = de->next;
					continue;
				}

				sorted_dict[pos++] = de;
				de = de->next;
			}
		}
	}

	/*
	 * Sort the flattened view of the hash in descending order of
	 * occurrence X word size.
	 */
	qsort(sorted_dict, pos, sizeof (dict_entry_t *), cmpoccur);
	num_entries = 0;
	new_size = size;

	for (i=0; i<pos; i++) {
		dict_entry_t *de;
		ssize_t prev_size;

		de = sorted_dict[i];
		if (de->occur > 1) {
			ssize_t val;

			/*
			 * Mark entries for which the encoded representation will be
			 * larger than the original.
			 */
			prev_size = new_size;
			val = (size_t)de->occur * (size_t)de->sz;
			new_size -= val;
			if (num_entries == 0)
				new_size += ((size_t)de->sz + (size_t)de->occur * 1);
			else if (num_entries < NUMERAL_BASE)
				new_size += ((size_t)de->sz + (size_t)de->occur * 2);
			else if (num_entries < NUMERAL_BASE * NUMERAL_BASE)
				new_size += ((size_t)de->sz + (size_t)de->occur * 3);
			else if (num_entries < NUMERAL_BASE * NUMERAL_BASE * NUMERAL_BASE)
				new_size += ((size_t)de->sz + (size_t)de->occur * 4);
			else
				new_size += ((size_t)de->sz + (size_t)de->occur * 5);
			if (new_size >= prev_size) {
				new_size = prev_size;
				de->occur = 0;
				continue;
			}

			de->indx = num_entries;
			num_entries++;
		} else {
			de->occur = 0;
		}
	}

	sz = sizeof (num_dict);
	numd = to_base_enc(num_entries, num_dict, sz);
	dstSize = num_dict+sz-numd-1;
	copy_bytes(dst, numd, dstSize);
	dst[dstSize++] = ' ';

	/*
	 * Copy the dictionary to the output buffer.
	 */
	for (i=0; i<pos && dstSize<*dstsize; i++) {
		dict_entry_t *de;

		de = sorted_dict[i];
		if (de->occur > 1) {
			dst[dstSize++] = de->lcfirst;
			if (dstSize + de->sz + 1 >= *dstsize) {
				goto bail;
			}

			copy_bytes(&dst[dstSize], de->word+1, de->sz-1);
			dstSize += (de->sz-1);
			dst[dstSize++] = ' ';
		}
	}

	pos = 0;
	for (i=0; i<size && dstSize<*dstsize; i++) {
		uint8_t *tok, c;

		c = src[i];
		if (SEPARATOR[c]) {
			dict_entry_t *de;
			size_t toklen = i - pos;

			if (toklen < WORD_MIN || toklen > WORD_MAX) {
				if (*(src+pos) == flag || *(src+pos) == flag1 ||
				    *(src+pos) == flag2 || *(src+pos) == '\\') {
					dst[dstSize++] = '\\';
				}
				if (dstSize + toklen + 1 > *dstsize) {
					goto bail;
				}
				copy_bytes(&dst[dstSize], src+pos, toklen+1);
				dstSize += (toklen+1);
				pos = i+1;
				continue;
			}

			tok = src+pos;
			de = hash_lookup(&hctx, tok, toklen, tolower(tok[0]));
			if (de != NULL && de->occur > 1) {
				uint16_t val;
				unsigned char tok_hdr[10], *dnum;

				/*
				 * Encode word with dictionary reference.
				 */
				sz = sizeof (tok_hdr);
				val = de->indx;
				dnum = to_base_enc(val, tok_hdr, sz);
				dnum--;
				if (isupper(tok[0])) {
					*dnum = flag1;
				} else {
					*dnum = flag;
				}

				val = tok_hdr+sz - dnum-1;
				if (dstSize + val + 1 > *dstsize) {
					goto bail;
				}
				copy_bytes(&dst[dstSize], dnum, val);
				dstSize += val;
				dst[dstSize++] = src[i];
			} else {
				uint8_t *word = src+pos;
				uint8_t num[15];
				uint32_t val;
				int converted;

				/*
				 * Encode literal numeric strings.
				 */
				converted = 0;
				if (word[0] != '+' && word[0] != '-' && word[0] != '0' &&
				    toklen > 4 && toklen < 10) {
					copy_bytes(num, word, toklen);
					num[toklen] = '\0';
					val = strtoul((const char *)num, (char **)&word, 10);

					if (*word == '\0') {
						uint8_t tok_hdr[10], *dnum;
						sz = sizeof (tok_hdr);
						dnum = to_base_enc(val, tok_hdr, sz);
						dnum--;
						*dnum = flag2;

						val = tok_hdr+sz - dnum-1;
						if (dstSize + val + 1 > *dstsize) {
							goto bail;
						}
						copy_bytes(&dst[dstSize], dnum, val);
						dstSize += val;
						dst[dstSize++] = src[i];
						converted = 1;
					}
				}
				if (!converted) {
					if (*(src+pos) == flag || *(src+pos) == flag1 ||
					    *(src+pos) == flag2 || *(src+pos) == '\\') {
						dst[dstSize++] = '\\';
					}
					if (dstSize + toklen + 1 > *dstsize) {
						goto bail;
					}
					copy_bytes(&dst[dstSize], src+pos, toklen+1);
					dstSize += (toklen+1);
				}
			}
			pos = i+1;
		}
	}
	if (pos < size) {
		uint32_t sz = size - pos;

		if (dstSize + sz > *dstsize) {
			goto bail;
		}
		copy_bytes(&dst[dstSize], src+pos, sz);
		dstSize += sz;
	}

	*dstsize = dstSize;
	rv = 1;

bail:
	hash_context_delete(&hctx);
	list_context_delete(&lctx);
	delete sorted_dict;

	return rv;
}

int
DictFilter::Inverse_Dict(uint8_t *src, uint32_t srclen, uint8_t *dst, uint32_t *dstsize)
{
	uint32_t numWords, i, enclen, pos;
	uint8_t *srcpos, *end, *dstpos, *dstend, c;
	decode_dict_entry_t *w_dict;

	end = src + srclen;
	srcpos = (uint8_t *)strchr((const char *)src, ' ');
	if (srcpos - src > 12) {
		return (0);
	}

	numWords = from_base_enc(src, srcpos - src);
	srcpos++;
	w_dict = new decode_dict_entry_t[numWords];
	for (i = 0; i < numWords && srcpos < end; i++) {
		uint8_t *w_src = srcpos;
		srcpos = (uint8_t *)strchr((const char *)srcpos, ' ');
		if (srcpos - w_src > WORD_MAX)
			return (0);

		w_dict[i].sz = srcpos - w_src;
		w_dict[i].word = w_src;
		srcpos++;
	}

	enclen = srclen - (srcpos - src);
	dstpos = dst;
	dstend = dst + *dstsize;
	pos = 0;

	for (i = 0; i < enclen && dstpos < dstend; i++) {
		c = srcpos[i];
		if (SEPARATOR[c]) {
			uint32_t toklen = i - pos;
			uint32_t dpos;

			c = srcpos[pos];
			if (toklen == 0) {
				*dstpos++ = srcpos[i];

			} else if (c == '\\') {
				if (dstpos + toklen > dstend) {
					log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
					return (0);
				}
				copy_bytes(dstpos, srcpos+pos+1, toklen);
				dstpos += toklen;

			} else if (c == flag) {
				toklen--;
				dpos = from_base_enc(srcpos+pos+1, toklen);

				if (dstpos + w_dict[dpos].sz > dstend) {
					log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
					return (0);
				}
				copy_bytes(dstpos, w_dict[dpos].word, w_dict[dpos].sz);
				dstpos += w_dict[dpos].sz;
				*dstpos++ = srcpos[i];

			} else if (c == flag1) {
				toklen--;
				dpos = from_base_enc(srcpos+pos+1, toklen);

				if (dstpos + w_dict[dpos].sz > dstend) {
					log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
					return (0);
				}
				*dstpos++ = toupper(*(w_dict[dpos].word));
				copy_bytes(dstpos, w_dict[dpos].word+1, w_dict[dpos].sz-1);
				dstpos += (w_dict[dpos].sz-1);
				*dstpos++ = srcpos[i];

			} else if (c == flag2) {
				uint32_t n;

				toklen--;
				dpos = from_base_enc(srcpos+pos+1, toklen);
				n = snprintf((char *)dstpos, dstend - dstpos, "%u", dpos);

				if (n >= dstend - dstpos) {
					log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
					return (0);
				}
				dstpos += n;
				*dstpos++ = srcpos[i];
			} else {
				if (dstpos + toklen + 1 > dstend) {
					log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
					return (0);
				}
				copy_bytes(dstpos, srcpos+pos, toklen+1);
				dstpos += (toklen+1);
			}
			pos = i+1;
		}
	}

	if (pos < i) {
		if (dstpos + i - pos > dstend) {
			log_msg(LOG_ERR, 0, "Overflow in DICT decode.\n");
			return (0);
		}
		copy_bytes(dstpos, srcpos+pos, i-pos);
		dstpos += (i-pos);
	}

	*dstsize = dstpos - dst;
	return (1);
}

#ifdef  __cplusplus
extern "C" {
#endif

int
dict_encode(uint8_t *from, uint64_t fromlen, uint8_t *to, uint64_t *dstlen)
{
	DictFilter *df = DictFilter::getInstance();
	u32 fl;
	u32 dl;
	uint8_t *dst;
	DEBUG_STAT_EN(double strt, en);

	/*
	 * Dict can't handle > 4GB buffers :-O
	 */
	if (fromlen > UINT32_MAX)
		return (-1);

	fl = (u32)fromlen;
	dl = (u32)(*dstlen);
	DEBUG_STAT_EN(strt = get_wtime_millis());
	U32_P(to) = LE32(fl);
	dst = to + 4;
	dl -= 4;
	if (df->Forward_Dict(from, fl, dst, &dl)) {
		*dstlen = dl + 4;
		DEBUG_STAT_EN(en = get_wtime_millis());
		DEBUG_STAT_EN(fprintf(stderr, "DICT: fromlen: %" PRIu64 ", dstlen: %" PRIu64 "\n",
				      fromlen, *dstlen));
		DEBUG_STAT_EN(fprintf(stderr, "DICT: Processed at %.3f MB/s\n",
				      get_mb_s(fromlen, strt, en)));
		return (1);
	}
	DEBUG_STAT_EN(fprintf(stderr, "No DICT\n"));
	return (-1);
}

int
dict_decode(uint8_t *from, uint64_t fromlen, uint8_t *to, uint64_t *dstlen)
{
	DictFilter *df = DictFilter::getInstance();
	u32 fl;
	u32 dl;
	u8 *src;
	int rv;
	DEBUG_STAT_EN(double strt, en);

	if (fromlen > UINT32_MAX) {
		log_msg(LOG_ERR, 0, "Dict decode buffer too big!");
		return (-1);
	}

	fl = (u32)fromlen;
	DEBUG_STAT_EN(strt = get_wtime_millis());
	dl = U32_P(from);
	if (dl > *dstlen) {
		log_msg(LOG_ERR, 0, "Destination overflow in dict_decode. Need: %" PRIu64 ", Got: %" PRIu64 "\n",
		    dl, *dstlen);
		return (-1);
	}
	*dstlen = dl;
	src = from + 4;
	fl -= 4;

	rv = df->Inverse_Dict(src, fl, to, &dl);
	if (!rv) {
		log_msg(LOG_ERR, 0, "dict_decode: Failed.\n");
		return (-1);
	}
	if (dl < *dstlen) {
		log_msg(LOG_ERR, 0, "dict_decode: Expected: %" PRIu64 ", Got: %" PRIu64 "\n",
		    *dstlen, dl);
		return (-1);
	}
	DEBUG_STAT_EN(en = get_wtime_millis());
	DEBUG_STAT_EN(fprintf(stderr, "DICT: fromlen: %" PRIu64 ", dstlen: %" PRIu64 "\n",
	    fromlen, *dstlen));
	DEBUG_STAT_EN(fprintf(stderr, "DICT: Processed at %.3f MB/s\n",
	    get_mb_s(fromlen, strt, en)));
	return (0);
}

#ifdef  __cplusplus
}
#endif
