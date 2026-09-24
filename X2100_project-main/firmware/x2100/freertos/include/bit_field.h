#ifndef _BIT_FIELD_H_
#define _BIT_FIELD_H_

/**
 * the max value of bit field "[start, end]"
 */
static inline unsigned long bit_field_max(int start, int end)
{
    unsigned long e = (1ul << end);
    unsigned long s = (1ul << start);
    return ((e - s) + e) >> start;
}

/**
 * the mask of bit field
 * mask = bit_field_max(start, end) << start;
 */
static inline unsigned long bit_field_mask(int start, int end)
{
    unsigned long e = (1ul << end);
    unsigned long s = (1ul << start);
    return (e - s) + e;
}

/**
 * check value is valid for the bit field
 * return bit_field_max(start, end) >= val;
 */
static inline int check_bit_field(int start, int end, unsigned long val)
{
    return bit_field_max(start, end) >= val;
}

/**
 * return the bitfield value
 */
static inline unsigned long bit_field_val(int start, int end, unsigned long val)
{
    unsigned long mask = bit_field_mask(start, end);
    return ((val << start) & mask);
}

/**
 * set value to the bit field
 * reg[start, end] = val;
 * @attention: this function do not check the value is valid for the bit field or not
 */
static inline void set_bit_field(unsigned long *reg, int start, int end, unsigned long val)
{
    unsigned long mask = bit_field_mask(start, end);
    *reg = (*reg & ~mask) | ((val << start) & mask);
}

/**
 * get value in the bit field
 * return reg[start, end];
 */
static inline unsigned long get_bit_field(unsigned long *reg, int start, int end)
{
    return (*reg & bit_field_mask(start, end)) >> start;
}


/**
 * set value to the bit field
 * reg[start, end] = val;
 * @attention: this function do not check the value is valid for the bit field or not
 * reg is volatile
 */
static inline void set_bit_field_v(volatile unsigned long *reg, int start, int end, unsigned long val)
{
    unsigned long mask = bit_field_mask(start, end);
    *reg = (*reg & ~mask) | ((val << start) & mask);
}

/**
 * get value in the bit field
 * return reg[start, end];
 * reg is volatile
 */
static inline unsigned long get_bit_field_v(volatile unsigned long *reg, int start, int end)
{
    return (*reg & bit_field_mask(start, end)) >> start;
}

///////////////////////////////////////////////////////////////////////////////

static inline int test_bit(int bit, const unsigned int *data)
{
	int i = bit / 32;
	int off = bit % 32;
	return !!(data[i] & (1 << off));
}

static inline unsigned int find_next_zero_bit(const unsigned int *data,
		unsigned int size, unsigned int offset)
{
	int i;

	for (i = offset; i < size; i++) {
		if (!test_bit(i, data))
			break;
	}

	return i;
}

static inline unsigned int find_next_bit(const unsigned int *data,
		unsigned int size, unsigned int offset)
{
	int i;

	for (i = offset; i < size; i++) {
		if (test_bit(i, data))
			break;
	}

	return i;
}

static inline void set_bit(int bit, unsigned int *data)
{
	int i = bit / 32;
	int off = bit % 32;
	data[i] |= (1 << off);
}

static inline void clear_bit(int bit, unsigned int *data)
{
	int i = bit / 32;
	int off = bit % 32;
	data[i] &= ~(1 << off);
}

static inline unsigned int
bitmap_find_next_zero_area(unsigned int *map, unsigned int size,
		unsigned int start, unsigned int nr)
{
	unsigned int index, end, i;
again:
	index = find_next_zero_bit(map, size, start);

	end = index + nr;
	if (end > size)
		return end;
	i = find_next_bit(map, end, index);
	if (i < end) {
		start = i + 1;
		goto again;
	}
	return index;
}

static inline void bitmap_set(unsigned int *map, unsigned int start, int len)
{
	int i;

	for (i = start; i < (start + len); i++)
		set_bit(i, map);
}

static inline void bitmap_clear(unsigned int *map, unsigned int start, int len)
{
	int i;

	for (i = start; i < (start + len); i++)
		clear_bit(i, map);
}


#endif /* _BIT_FIELD_H_ */
