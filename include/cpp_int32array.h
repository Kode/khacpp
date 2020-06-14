#pragma once

struct int32array {
	int* data;
	int myLength;

	int32array() {
		data = NULL;
	}

	void alloc(int elements) {
		myLength = elements;
		data = new int[myLength];
	}

	void free() {
		delete[] data;
		data = NULL;
	}

	int length() {
		return myLength;
	}

	short get(int index) {
		return data[index];
	}

	short set(int index, short value) {
		return data[index] = value;
	}
};
