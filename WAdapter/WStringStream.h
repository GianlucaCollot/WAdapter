#ifndef _STRING_STREAM_H_
#define _STRING_STREAM_H_

#include <Stream.h>

class WStringStream : public Stream {
public:
    WStringStream(unsigned int maxLength) {
    	this->maxLength = maxLength;
    	this->string = new char[maxLength + 1];
    	this->flush();
    }

    ~WStringStream() {
    	if(this->string) {
    		delete[] this->string;
    	}
    }

    // Stream methods
    virtual int available() {
    	return getMaxLength() - position;
    }

    virtual int read() {
    	if (position > 0) {
    		char c = string[0];
    		for (int i = 1; i <= position; i++) {
    			string[i - 1] = string[i];
    		}
			position--;
    		return c;
    	}
    	return -1;
    }

    virtual int peek() {
    	if (position > 0) {
    	    char c = string[0];
    	    return c;
    	}
    	return -1;
    }

    virtual void flush() {
    	this->position = 0;
    	this->string[0] = '\0';
    }

    // Print methods
    virtual size_t write(uint8_t c) {
    	if (position < maxLength) {
    		string[position] = (char) c;
    		position++;
    		string[position] = '\0';
    		return 1;
    	} else {
    		return 0;
    	}
    }

    unsigned int length() {
        return this->position;
    }

    unsigned int getMaxLength() {
    	return this->maxLength;
    }

    char* c_str() {
    	return this->string;
    }

private:
    char* string;
    unsigned int maxLength;
    unsigned int position;
};

#endif // _STRING_STREAM_H_