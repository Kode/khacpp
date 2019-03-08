#pragma once

struct uint32array {
	unsigned int* data;
	int myLength;

	void alloc(int elements) {
		myLength = elements;
		data = new unsigned int[myLength];
	}

	void free() {
		delete[] data;
		data = NULL;
	}

	int length() {
		return myLength;
	}

	unsigned int get(int index) {
		return data[index];
	}

	unsigned int set(int index, unsigned int value) {
		return data[index] = value;
	}
};
