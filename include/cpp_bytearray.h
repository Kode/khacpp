#pragma once

#include <stdint.h>

struct bytearray {
	uint8_t *data;
	int myLength;

	bytearray() {
		data = NULL;
	}

	void alloc(int elements) {
		myLength = elements;
		data = new uint8_t[myLength];
	}

	void free() {
		delete[] data;
		data = NULL;
	}

	int byteLength() {
		return myLength;
	}

	float get(int index) {
		return data[index];
	}

	float set(int index, float value) {
		return data[index] = value;
	}

	bytearray slice(int begin, int end) {
		bytearray arr;
		arr.data = &data[begin];
		arr.myLength = end - begin;
		return arr;
	}
};
