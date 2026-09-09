/*
 * Escape decoding for badge, caption, and text fields (\xNN, \uNNNN,
 * \UNNNNNNNN, \\).  Shared by the client library and the daemon.
 */
#include <stddef.h>

#include <bosd.h>

void	 decode_escapes(const char *in, char *out, size_t outlen);

static int
xdigit(int c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

static size_t
utf8_put(char *out, unsigned long cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return (1);
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return (2);
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xe0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return (3);
	}
	if (cp < 0x110000) {
		out[0] = (char)(0xf0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[3] = (char)(0x80 | (cp & 0x3f));
		return (4);
	}
	return (0);
}

/*
 * Decode \xNN (raw byte), \uNNNN and \UNNNNNNNN (codepoint, UTF-8
 * encoded), and \\.  Malformed escapes pass through literally.
 */
void
decode_escapes(const char *in, char *out, size_t outlen)
{
	size_t o = 0;

	while (*in != '\0' && o + 5 < outlen) {
		unsigned long cp = 0;
		int i, n, v;

		if (in[0] != '\\') {
			out[o++] = *in++;
			continue;
		}
		switch (in[1]) {
		case '\\':
			out[o++] = '\\';
			in += 2;
			continue;
		case 'x':
			n = 2;
			break;
		case 'u':
			n = 4;
			break;
		case 'U':
			n = 8;
			break;
		default:
			out[o++] = *in++;
			continue;
		}
		for (i = 0; i < n; i++) {
			v = xdigit((unsigned char)in[2 + i]);
			if (v < 0)
				break;
			cp = cp * 16 + (unsigned long)v;
		}
		if (i < n) {
			out[o++] = *in++;
			continue;
		}
		if (in[1] == 'x')
			out[o++] = (char)cp;
		else
			o += utf8_put(out + o, cp);
		in += 2 + n;
	}
	out[o] = '\0';
}
