#pragma once

struct uint8array {
	unsigned char* data;
	int myLength;

	uint8array() {
		data = NULL;
	}

	void alloc(int elements) {
		myLength = elements;
		data = new unsigned char[myLength];
	}

	void free() {
		delete[] data;
		data = NULL;
	}

	int length() {
		return myLength;
	}

	unsigned char get(int index) {
		return data[index];
	}

	unsigned char set(int index, unsigned char value) {
		return data[index] = value;
	}
};
