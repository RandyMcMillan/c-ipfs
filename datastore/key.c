#include <string.h>
#include <ctype.h>

/**
 * Constructs a new "clean" key. Will remove things like slashes
 * @param input the input
 * @param output the output
 * @param max_output_length the amount of memory allocated for output
 * @param actual_output_length the amount of bytes written to output
 * @returns true(1) on success
 */
int ipfs_datastore_key_new(const char* input, char* output, size_t max_output_length, size_t* actual_output_length) {
	if (!input || !output || max_output_length == 0)
		return 0;

	size_t in_len = strlen(input);
	if (in_len == 0 || in_len + 1 > max_output_length)
		return 0;

	/* Sanitize: collapse duplicate slashes and ensure leading slash */
	size_t j = 0;
	int last_was_slash = 0;
	for (size_t i = 0; i < in_len; i++) {
		char c = input[i];
		if (c == '/' || c == '\\') {
			if (!last_was_slash) {
				if (j < max_output_length - 1)
					output[j++] = '/';
			}
			last_was_slash = 1;
		} else if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') {
			if (j < max_output_length - 1)
				output[j++] = c;
			last_was_slash = 0;
		}
		/* Discard other characters */
	}

	/* Ensure leading slash */
	if (j == 0 || output[0] != '/') {
		if (j + 1 < max_output_length) {
			memmove(output + 1, output, j);
			output[0] = '/';
			j++;
		} else if (max_output_length > 1) {
			output[0] = '/';
			j = 1;
		}
	}

	/* Strip trailing slash unless key is just "/" */
	if (j > 1 && output[j - 1] == '/')
		j--;

	output[j] = '\0';
	if (actual_output_length)
		*actual_output_length = j;
	return 1;
}
