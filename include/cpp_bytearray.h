#pragma once

#include <assert.h>
#include <stdint.h>

struct bytearray {
	uint8_t *data;
	int refCount;

	bytearray() {
		data = NULL;
		refCount = 0;
	}

	void alloc(int length) {
		data = new uint8_t[length];
	}

	void addRef() {
		++refCount;
	}

	void subRef() {
		--refCount;
		if (refCount == 0) {
			delete[] data;
			data = NULL;
		}
	}

	float get(int index) {
		return data[index];
	}

	float set(int index, float value) {
		return data[index] = value;
	}
};
