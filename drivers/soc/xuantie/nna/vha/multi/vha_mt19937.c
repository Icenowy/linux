/*
	 A C-program for MT19937, with initialization improved 2002/1/26.
	 Coded by Takuji Nishimura and Makoto Matsumoto.

	 Copyright (C) 1997 - 2002, Makoto Matsumoto and Takuji Nishimura,
	 All rights reserved.
	 Copyright (C) 2005, Mutsuo Saito,
	 All rights reserved.

	 Redistribution and use in source and binary forms, with or without
	 modification, are permitted provided that the following conditions
	 are met:

		 1. Redistributions of source code must retain the above copyright
				notice, this list of conditions and the following disclaimer.

		 2. Redistributions in binary form must reproduce the above copyright
				notice, this list of conditions and the following disclaimer in the
				documentation and/or other materials provided with the distribution.

		 3. The names of its contributors may not be used to endorse or promote
				products derived from this software without specific prior written
				permission.

	 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
	 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
	 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
	 A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
	 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
	 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
	 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
	 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
	 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
	 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
	 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


	 Any feedback is very welcome.
	 http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
	 email: m-mat @ math.sci.hiroshima-u.ac.jp (remove space)

	 Original code available at:
	 http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html
*/

/*
 *****************************************************************************
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/slab.h>

#include "vha_mt19937.h"

/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */

struct vha_mt19937_ctx
{
	uint32_t mt[N];    /*!< The array for the state vector.    */
	int32_t  mti;      /*!<                                    */
	uint32_t mag01[2]; /*!< mag01[x] = x * MATRIX_A  for x=0,1 */
};

int vha_mt19937_init(uint32_t seed, void **handle)
{
	struct vha_mt19937_ctx *ctx;

	/* Check input params. */
	if (NULL == handle)	{
		pr_err("%s: invalid handle\n", __func__);
		return -EINVAL;
	}

	/* Allocate SFMT context. */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL) {
		pr_err("%s: failed to allocate context\n", __func__);
		return -ENOMEM;
	}

	/* Initialise SFMT context with the seed. */
	ctx->mt[0]= seed & 0xffffffffUL;
	for (ctx->mti = 1; ctx->mti < N; ctx->mti++) {
		ctx->mt[ctx->mti] =
			(1812433253UL * (ctx->mt[ctx->mti - 1] ^ (ctx->mt[ctx->mti - 1] >> 30)) + ctx->mti);
		/* See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier. */
		/* In the previous versions, MSBs of the seed affect   */
		/* only MSBs of the array mt[].                        */
		/* 2002/01/09 modified by Makoto Matsumoto             */
		ctx->mt[ctx->mti] &= 0xffffffffUL;
		/* for >32 bit machines */
	}

	ctx->mag01[0] = 0x0UL;
	ctx->mag01[1] = MATRIX_A;

	*handle = ctx;

	return 0;
}

int vha_mt19937_deinit(void *handle)
{
	struct vha_mt19937_ctx *ctx = (struct vha_mt19937_ctx*)handle;

	/* Check input params. */
	if (NULL == handle)	{
		pr_err("%s: invalid handle\n", __func__);
		return -EINVAL;
	}

	/* Free the SFMT context. */
	kfree(ctx);

	return 0;
}

int vha_mt19937_gen_uint32(void *handle, uint32_t *rand_val)
{
	struct vha_mt19937_ctx *ctx = (struct vha_mt19937_ctx*)handle;
	uint32_t y;

	/* Check input params. */
	if (NULL == handle) {
		pr_err("%s: invalid handle\n", __func__);
		return -EINVAL;
	}
	if (NULL == rand_val) {
		pr_err("%s: invalid rand_val\n", __func__);
		return -EINVAL;
	}

	/* Generate N words at one time. */
	if (ctx->mti >= N) {
		int kk;

		for (kk = 0; kk < (N - M); kk++) {
			y = (ctx->mt[kk] & UPPER_MASK) | (ctx->mt[kk + 1] & LOWER_MASK);
			ctx->mt[kk] = ctx->mt[kk + M] ^ (y >> 1) ^ ctx->mag01[y & 0x1UL];
		}
		for (; kk < (N - 1); kk++) {
			y = (ctx->mt[kk] & UPPER_MASK) | (ctx->mt[kk + 1] & LOWER_MASK);
			ctx->mt[kk] = ctx->mt[kk + (M - N)] ^ (y >> 1) ^ ctx->mag01[y & 0x1UL];
		}
		y = (ctx->mt[N - 1] & UPPER_MASK) | (ctx->mt[0] & LOWER_MASK);
		ctx->mt[N - 1] = ctx->mt[M - 1] ^ (y >> 1) ^ ctx->mag01[y & 0x1UL];

		ctx->mti = 0;
	}

	y = ctx->mt[ctx->mti++];

	/* Tempering */
	y ^= (y >> 11);
	y ^= (y << 7) & 0x9d2c5680UL;
	y ^= (y << 15) & 0xefc60000UL;
	y ^= (y >> 18);

	*rand_val = y;

	return 0;
}

int vha_mt19937_gen_range(void *handle, uint32_t min, uint32_t max, uint32_t *rand_val)
{
	int ret;
	uint32_t range_width;

	/* Generate 32-bit random value. */
	ret = vha_mt19937_gen_uint32(handle, rand_val);
	if (ret != 0)
		return ret;

	/* Calculate the range. */
	range_width = abs(max - min);
	if (min > max)
	{
		min = max;
	}

	/* Calculate the random value within range. */
	*rand_val = (range_width > 0) ? ((*rand_val % (range_width + 1)) + min) : min;

	return 0;
}


