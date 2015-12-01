#include "kstdlib.h"

void* memcpy(void* destination, const void* source, size_t num) {
	uint8_t* src = (uint8_t*) source;
	uint8_t* dst = (uint8_t*) destination;
	while (num--)
		*dst++ = *src++;
	return destination;
}

void* memset(void* pointer, uint8_t value, size_t num) {
	uint8_t* ptr = (uint8_t*) pointer;
	uint32_t i;
	for (i = 0; i < num; i++) {
		ptr[i] = value;
	}
	return pointer;
}

uint32_t strlen(const char* string) {
	uint32_t size = 0;
	char* test = (char*) string;
	while (*test++)
		++size;
	return size;
}

cmpresult_t strcmp(const char* first, const char* second) {
	uint32_t i = 0;
	char a, b;

	while ((a = first[i]) == (b = second[i]) &&a != 0 && b != 0)
		i++;
	return a < b ? -1 : a > b ? 1 : 0;
}

char* strcpy(char* target, const char* source) {
	char* returnee = target;
	char* buffer = (char*) source;
	while ((*target++ = *buffer++))
		;
	return returnee;
}

inline uint32_t rand_number(int32_t limit) {
	static uint32_t a = 3;
	a = (((a * 214013L + 2531011L) >> 16) & 32767);
	return ((a % limit));
}

static char bytetohex(uint8_t byte) {
	switch (byte) {
	case 10:
		return 'A';
	case 11:
		return 'B';
	case 12:
		return 'C';
	case 13:
		return 'D';
	case 14:
		return 'E';
	case 15:
		return 'F';
	default:
		return '0' + byte;
	}
}

char conv[11];

const char* hextochar(uint32_t num) {

	conv[0] = '0';
	conv[1] = 'x';
	conv[2] = bytetohex((num / (1 << 24)) / 16);
	conv[3] = bytetohex((num / (1 << 24)) % 16);
	conv[4] = bytetohex((num / (1 << 16)) / 16);
	conv[5] = bytetohex((num / (1 << 16)) % 16);
	conv[6] = bytetohex((num / (1 << 8)) / 16);
	conv[7] = bytetohex((num / (1 << 8)) % 16);
	conv[8] = bytetohex((num % 256) / 16);
	conv[9] = bytetohex((num % 256) % 16);
	conv[10] = 0;

	return conv;
}

uint32_t int_hash_function(void* integer) {
#ifdef KERNEL64BIT
	uint32_t a = (uint32_t) ((uint64_t)integer);
#else
	uint32_t a = (uint32_t) (integer);
#endif
	a = (a + 0x7ed55d16) + (a << 12);
	a = (a ^ 0xc761c23c) ^ (a >> 19);
	a = (a + 0x165667b1) + (a << 5);
	a = (a + 0xd3a2646c) ^ (a << 9);
	a = (a + 0xfd7046c5) + (a << 3);
	a = (a ^ 0xb55a4f09) ^ (a >> 16);
	return a;
}

bool int_cmpr_function(void* a, void* b) {
	return a == b ? true : false;
}

