#pragma once

struct float32array {
	float* data;
	int myLength;

	void alloc(int elements) {
		myLength = elements;
		data = new float[myLength];
	}

	void free() {
		delete[] data;
		data = NULL;
	}

	int length() {
		return myLength;
	}

	float get(int index) {
		return data[index];
	}

	float set(int index, float value) {
		return data[index] = value;
	}
};
