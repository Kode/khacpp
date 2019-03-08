#pragma once

struct int16array {
	short* data;
	int myLength;

	void alloc(int elements) {
		myLength = elements;
		data = new short[myLength];
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
