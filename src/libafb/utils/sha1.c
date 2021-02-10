/*
 * sha1.c
 *
 * Originally witten by Steve Reid <steve@edmweb.com>
 *
 * Modified by Aaron D. Gifford <agifford@infowest.com>
 *
 * Modified by José Bollo <jose.bollo@iot.bzh>
 *
 * NO COPYRIGHT - THIS IS 100% IN THE PUBLIC DOMAIN
 *
 * The original unmodified version is available at:
 *    ftp://ftp.funet.fi/pub/crypt/hash/sha/sha1.c
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <stdint.h>
#include <string.h>

#include "sha1.h"

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define blk0(i) (block[i] = (rol(block[i],24)&(uint32_t)0xFF00FF00) \
	|(rol(block[i],8)&(uint32_t)0x00FF00FF))
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define blk0(i) block[i]
#else
# error "unsupported byte order"
#endif

#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15] \
	^block[(i+2)&15]^block[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)+blk(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk(i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=(w^x^y)+blk(i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

/* Hash a single 512-bit block. This is the core of the algorithm. */
static void transform(uint32_t state[5], uint32_t block[16])
{
	uint32_t a, b, c, d, e;

	/* Copy context->state[] to working vars */
	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];
	e = state[4];
	/* 4 rounds of 20 operations each. Loop unrolled. */
	R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
	R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
	R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
	R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
	R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
	R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
	R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
	R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
	R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
	R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
	R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
	R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
	R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
	R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
	R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
	R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
	R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
	R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
	R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
	R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
	/* Add the working vars back into context.state[] */
	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
#if WIPE
	/* Wipe variables */
	a = b = c = d = e = 0;
#endif
}


/* SHA1_Init - Initialize new context */
void SHA1_init(SHA1_t* context)
{
	/* SHA1 initialization constants */
	context->state[0] = 0x67452301;
	context->state[1] = 0xEFCDAB89;
	context->state[2] = 0x98BADCFE;
	context->state[3] = 0x10325476;
	context->state[4] = 0xC3D2E1F0;
	context->length = 0;
}

/* Run your data through this. */
void SHA1_update(SHA1_t *context, const void *buffer, size_t len)
{
	size_t i, j;
	const uint8_t *data = buffer;

	if (len) {
		j = (size_t)(context->length & 63);
		context->length += (uint64_t)len;

		if (j + len < 64)
			memcpy(&context->buffer[j], data, len);
		else {
			i = 64 - j;
			memcpy(&context->buffer[j], data, i);
			transform(context->state, context->blocks);
			for ( ; i + 64 <= len; i += 64) {
				memcpy(context->buffer, &data[i], 64);
				transform(context->state, context->blocks);
			}
			if (len - i)
				memcpy(context->buffer, &data[i], len - i);
		}
	}
}


/* Add padding and return the message digest. */
void SHA1_final(SHA1_t *context, uint8_t digest[SHA1_DIGEST_LENGTH])
{
	uint32_t i;
	uint64_t len;
	uint8_t finalcount[8], c128 = 0200, c0 = 0;

	len = context->length;
	SHA1_update(context, &c128, 1);
	while ((context->length & 63) != 56)
	    SHA1_update(context, &c0, 1);

	/* Should cause a transform() */
	len <<= 3;
	for (i = 0; i < 8; i++)
		finalcount[i] = (uint8_t)((len >> ((7 - i) << 3)) & 255);
	SHA1_update(context, finalcount, 8);

	/* Get the result */
	for (i = 0; i < SHA1_DIGEST_LENGTH; i++) {
	    digest[i] = (uint8_t)
	     ((context->state[i >> 2] >> ((3 - (i & 3)) << 3)) & 255);
	}
#if WIPE
	/* Wipe variables */
	memset(&finalcount, 0, sizeof finalcount);
	memset(context, 0, sizeof *context);
#endif
}

#if TEST_SHA1
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
int main (int ac, char **av)
{
	char buf[400];
	SHA1_t s;
	int f, i;
	ssize_t r;
	uint8_t d[SHA1_DIGEST_LENGTH];

	while(*++av) {
		f = open(*av, O_RDONLY);
		if (f < 0)
			fprintf(stderr, "can't open %s\n", *av);
		else {
			SHA1_init(&s);
			r = read(f, buf, sizeof buf);
			while (r > 0) {
				SHA1_update(&s, buf, (size_t)r);
				r = read(f, buf, sizeof buf);
			}
			close(f);
			SHA1_final(&s, d);
			for (i=0 ; i < SHA1_DIGEST_LENGTH ; i++)
				printf("%02x", (int)d[i]);
			printf(" %s\n", *av);
		}
	}
	return 0;
}
#endif

