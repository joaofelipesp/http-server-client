#include <ctype.h>
#include <stdio.h>

#include "urlencode.h"

int urlEncode(const char *in, char *out, int n){
	int i = 0;

	if (!in || !out || n <= 0)
		return 1;

	while(*in){
		unsigned char c = (unsigned char)*in;

		if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'){
			if (1+i >= n) return 1;
			out[i++] = c;
		}else {
			if (3+i >= n)
				return 1;
			snprintf(out + i, 4, "%%%02X", c);
			i += 3;
		}
		in++;
	}

	out[i] = '\0';
	return 0;
}

int urlDecode(const char *in, char *out, int n){
	int i = 0;

	if(!in || !out || n <= 0)
		return 1;

	while(*in){
		if (1+i >= n)
			return 1;

		if(*in == '%'){
			if (
				!isxdigit((unsigned char)in[1]) ||
				!isxdigit((unsigned char)in[2])
			) return 1;

			int value;
			sscanf(1+in, "%2x", &value);
			out[i++] = (char)value;
			in += 3;
		}else if (*in == '+') {
			out[i++] = ' ';
			in++;
		}else {
			out[i++] = *in++;
		}
	}

	out[i] = '\0';
	return 0;
}