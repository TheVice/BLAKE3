#include "blake3_impl.h"

#include <immintrin.h>

#define DEGREE 8

INLINE __m256i rot16(__m256i x) {
  return _mm256_shuffle_epi8(
      x, _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
                         13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2));
}

INLINE __m256i rot12(__m256i x) {
  return _mm256_or_si256(_mm256_srli_epi32(x, 12), _mm256_slli_epi32(x, 32 - 12));
}

INLINE __m256i rot8(__m256i x) {
  return _mm256_shuffle_epi8(
      x, _mm256_set_epi8(12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1,
                         12, 15, 14, 13, 8, 11, 10, 9, 4, 7, 6, 5, 0, 3, 2, 1));
}

INLINE __m256i rot7(__m256i x) {
  return _mm256_or_si256(_mm256_srli_epi32(x, 7), _mm256_slli_epi32(x, 32 - 7));
}

INLINE void round_fn(__m256i v[16], __m256i m[16], size_t r) {

#define G(a,b,c,d,r,i) do {                                             \
  a = _mm256_add_epi32(a, m[MSG_SCHEDULE[r][2*i+0]]);                   \
  a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); d = rot16(d); \
  c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); b = rot12(b); \
  a = _mm256_add_epi32(a, m[MSG_SCHEDULE[r][2*i+1]]);                   \
  a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); d = rot8(d);  \
  c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); b = rot7(b);  \
} while(0)

  G(v[ 0], v[ 4], v[ 8], v[12], r, 0);
  G(v[ 1], v[ 5], v[ 9], v[13], r, 1);
  G(v[ 2], v[ 6], v[10], v[14], r, 2);
  G(v[ 3], v[ 7], v[11], v[15], r, 3);

  G(v[ 0], v[ 5], v[10], v[15], r, 4);
  G(v[ 1], v[ 6], v[11], v[12], r, 5);
  G(v[ 2], v[ 7], v[ 8], v[13], r, 6);
  G(v[ 3], v[ 4], v[ 9], v[14], r, 7);

#undef G
}

INLINE void transpose_vecs(__m256i vecs[DEGREE]) {
  // Interleave 32-bit lanes. The low unpack is lanes 00/11/44/55, and the high
  // is 22/33/66/77.
  __m256i ab_0145 = _mm256_unpacklo_epi32(vecs[0], vecs[1]);
  __m256i ab_2367 = _mm256_unpackhi_epi32(vecs[0], vecs[1]);
  __m256i cd_0145 = _mm256_unpacklo_epi32(vecs[2], vecs[3]);
  __m256i cd_2367 = _mm256_unpackhi_epi32(vecs[2], vecs[3]);
  __m256i ef_0145 = _mm256_unpacklo_epi32(vecs[4], vecs[5]);
  __m256i ef_2367 = _mm256_unpackhi_epi32(vecs[4], vecs[5]);
  __m256i gh_0145 = _mm256_unpacklo_epi32(vecs[6], vecs[7]);
  __m256i gh_2367 = _mm256_unpackhi_epi32(vecs[6], vecs[7]);

  // Interleave 64-bit lates. The low unpack is lanes 00/22 and the high is
  // 11/33.
  __m256i abcd_04 = _mm256_unpacklo_epi64(ab_0145, cd_0145);
  __m256i abcd_15 = _mm256_unpackhi_epi64(ab_0145, cd_0145);
  __m256i abcd_26 = _mm256_unpacklo_epi64(ab_2367, cd_2367);
  __m256i abcd_37 = _mm256_unpackhi_epi64(ab_2367, cd_2367);
  __m256i efgh_04 = _mm256_unpacklo_epi64(ef_0145, gh_0145);
  __m256i efgh_15 = _mm256_unpackhi_epi64(ef_0145, gh_0145);
  __m256i efgh_26 = _mm256_unpacklo_epi64(ef_2367, gh_2367);
  __m256i efgh_37 = _mm256_unpackhi_epi64(ef_2367, gh_2367);

  // Interleave 128-bit lanes.
  vecs[0] = _mm256_permute2x128_si256(abcd_04, efgh_04, 0x20);
  vecs[1] = _mm256_permute2x128_si256(abcd_15, efgh_15, 0x20);
  vecs[2] = _mm256_permute2x128_si256(abcd_26, efgh_26, 0x20);
  vecs[3] = _mm256_permute2x128_si256(abcd_37, efgh_37, 0x20);
  vecs[4] = _mm256_permute2x128_si256(abcd_04, efgh_04, 0x31);
  vecs[5] = _mm256_permute2x128_si256(abcd_15, efgh_15, 0x31);
  vecs[6] = _mm256_permute2x128_si256(abcd_26, efgh_26, 0x31);
  vecs[7] = _mm256_permute2x128_si256(abcd_37, efgh_37, 0x31);
}

INLINE void transpose_msg_vecs(const uint8_t *const *inputs,
                               size_t block_offset, __m256i out[16]) {
  out[ 0] = _mm256_loadu_si256((__m256i const *)&inputs[0][block_offset + 0 * sizeof(__m256i)]);
  out[ 1] = _mm256_loadu_si256((__m256i const *)&inputs[1][block_offset + 0 * sizeof(__m256i)]);
  out[ 2] = _mm256_loadu_si256((__m256i const *)&inputs[2][block_offset + 0 * sizeof(__m256i)]);
  out[ 3] = _mm256_loadu_si256((__m256i const *)&inputs[3][block_offset + 0 * sizeof(__m256i)]);
  out[ 4] = _mm256_loadu_si256((__m256i const *)&inputs[4][block_offset + 0 * sizeof(__m256i)]);
  out[ 5] = _mm256_loadu_si256((__m256i const *)&inputs[5][block_offset + 0 * sizeof(__m256i)]);
  out[ 6] = _mm256_loadu_si256((__m256i const *)&inputs[6][block_offset + 0 * sizeof(__m256i)]);
  out[ 7] = _mm256_loadu_si256((__m256i const *)&inputs[7][block_offset + 0 * sizeof(__m256i)]);
  out[ 8] = _mm256_loadu_si256((__m256i const *)&inputs[0][block_offset + 1 * sizeof(__m256i)]);
  out[ 9] = _mm256_loadu_si256((__m256i const *)&inputs[1][block_offset + 1 * sizeof(__m256i)]);
  out[10] = _mm256_loadu_si256((__m256i const *)&inputs[2][block_offset + 1 * sizeof(__m256i)]);
  out[11] = _mm256_loadu_si256((__m256i const *)&inputs[3][block_offset + 1 * sizeof(__m256i)]);
  out[12] = _mm256_loadu_si256((__m256i const *)&inputs[4][block_offset + 1 * sizeof(__m256i)]);
  out[13] = _mm256_loadu_si256((__m256i const *)&inputs[5][block_offset + 1 * sizeof(__m256i)]);
  out[14] = _mm256_loadu_si256((__m256i const *)&inputs[6][block_offset + 1 * sizeof(__m256i)]);
  out[15] = _mm256_loadu_si256((__m256i const *)&inputs[7][block_offset + 1 * sizeof(__m256i)]);
  for (size_t i = 0; i < 8; ++i) {
    _mm_prefetch(&inputs[i][block_offset + 256], _MM_HINT_T0);
  }
  transpose_vecs(&out[0]);
  transpose_vecs(&out[8]);
}

INLINE void load_counters(uint64_t counter, bool increment_counter,
                          __m256i *out_lo, __m256i *out_hi) {
  const __m256i mask = _mm256_set1_epi32(-(uint32_t)increment_counter);
  const __m256i add0 = _mm256_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
  const __m256i add1 = _mm256_and_si256(mask, add0);
  __m256i l = _mm256_add_epi32(_mm256_set1_epi32(counter), add1);
  __m256i carry = _mm256_cmpgt_epi32(_mm256_xor_si256(add1, _mm256_set1_epi32(0x80000000)), 
                                     _mm256_xor_si256(   l, _mm256_set1_epi32(0x80000000)));
  __m256i h = _mm256_sub_epi32(_mm256_set1_epi32(counter >> 32), carry);
  *out_lo = l;
  *out_hi = h;
}

void blake3_hash8_avx2(const uint8_t *const *inputs, size_t blocks,
                       const uint32_t key[8], uint64_t counter,
                       bool increment_counter, uint8_t flags,
                       uint8_t flags_start, uint8_t flags_end, uint8_t *out) {
  __m256i h_vecs[8] = {
      _mm256_set1_epi32(key[0]), _mm256_set1_epi32(key[1]), 
      _mm256_set1_epi32(key[2]), _mm256_set1_epi32(key[3]),
      _mm256_set1_epi32(key[4]), _mm256_set1_epi32(key[5]), 
      _mm256_set1_epi32(key[6]), _mm256_set1_epi32(key[7]),
  };
  __m256i counter_low_vec, counter_high_vec;
  load_counters(counter, increment_counter, &counter_low_vec,
                &counter_high_vec);
  // uint8_t block_flags = flags | flags_start;
  __m256i flags_      = _mm256_set1_epi32(flags);
  __m256i block_flags = _mm256_set1_epi32(flags | flags_start);
  __m256i block_end   = _mm256_set1_epi32(flags_end);
  __m256i block_len   = _mm256_set1_epi32(BLAKE3_BLOCK_LEN);

  for (size_t block = 0; block < blocks; block++) {
    if (block + 1 == blocks) {
      // block_flags |= flags_end;
      block_flags = _mm256_or_si256(block_flags, block_end);
    }
    __m256i msg_vecs[16];
    transpose_msg_vecs(inputs, block * BLAKE3_BLOCK_LEN, msg_vecs);

    __m256i v[16] = {
        h_vecs[0],       h_vecs[1],        h_vecs[2],     h_vecs[3],
        h_vecs[4],       h_vecs[5],        h_vecs[6],     h_vecs[7],
        _mm256_set1_epi32(IV[0]), _mm256_set1_epi32(IV[1]), _mm256_set1_epi32(IV[2]), _mm256_set1_epi32(IV[3]),
        counter_low_vec, counter_high_vec, block_len, block_flags,
    };
    round_fn(v, msg_vecs, 0);
    round_fn(v, msg_vecs, 1);
    round_fn(v, msg_vecs, 2);
    round_fn(v, msg_vecs, 3);
    round_fn(v, msg_vecs, 4);
    round_fn(v, msg_vecs, 5);
    round_fn(v, msg_vecs, 6);
    h_vecs[0] = _mm256_xor_si256(v[0], v[ 8]);
    h_vecs[1] = _mm256_xor_si256(v[1], v[ 9]);
    h_vecs[2] = _mm256_xor_si256(v[2], v[10]);
    h_vecs[3] = _mm256_xor_si256(v[3], v[11]);
    h_vecs[4] = _mm256_xor_si256(v[4], v[12]);
    h_vecs[5] = _mm256_xor_si256(v[5], v[13]);
    h_vecs[6] = _mm256_xor_si256(v[6], v[14]);
    h_vecs[7] = _mm256_xor_si256(v[7], v[15]);

    block_flags = flags_;
  }

  transpose_vecs(h_vecs);
  _mm256_storeu_si256((__m256i *)&out[0 * sizeof(__m256i)], h_vecs[0]);
  _mm256_storeu_si256((__m256i *)&out[1 * sizeof(__m256i)], h_vecs[1]);
  _mm256_storeu_si256((__m256i *)&out[2 * sizeof(__m256i)], h_vecs[2]);
  _mm256_storeu_si256((__m256i *)&out[3 * sizeof(__m256i)], h_vecs[3]);
  _mm256_storeu_si256((__m256i *)&out[4 * sizeof(__m256i)], h_vecs[4]);
  _mm256_storeu_si256((__m256i *)&out[5 * sizeof(__m256i)], h_vecs[5]);
  _mm256_storeu_si256((__m256i *)&out[6 * sizeof(__m256i)], h_vecs[6]);
  _mm256_storeu_si256((__m256i *)&out[7 * sizeof(__m256i)], h_vecs[7]);
}

#if !defined(BLAKE3_NO_SSE41)
void blake3_hash_many_sse41(const uint8_t *const *inputs, size_t num_inputs,
                            size_t blocks, const uint32_t key[8],
                            uint64_t counter, bool increment_counter,
                            uint8_t flags, uint8_t flags_start,
                            uint8_t flags_end, uint8_t *out);
#else
void blake3_hash_many_portable(const uint8_t *const *inputs, size_t num_inputs,
                               size_t blocks, const uint32_t key[8],
                               uint64_t counter, bool increment_counter,
                               uint8_t flags, uint8_t flags_start,
                               uint8_t flags_end, uint8_t *out);
#endif

void blake3_hash_many_avx2(const uint8_t *const *inputs, size_t num_inputs,
                           size_t blocks, const uint32_t key[8],
                           uint64_t counter, bool increment_counter,
                           uint8_t flags, uint8_t flags_start,
                           uint8_t flags_end, uint8_t *out) {
  while (num_inputs >= DEGREE) {
    blake3_hash8_avx2(inputs, blocks, key, counter, increment_counter, flags,
                      flags_start, flags_end, out);
    if (increment_counter) {
      counter += DEGREE;
    }
    inputs += DEGREE;
    num_inputs -= DEGREE;
    out = &out[DEGREE * BLAKE3_OUT_LEN];
  }
#if !defined(BLAKE3_NO_SSE41)
  blake3_hash_many_sse41(inputs, num_inputs, blocks, key, counter,
                         increment_counter, flags, flags_start, flags_end, out);
#else
  blake3_hash_many_portable(inputs, num_inputs, blocks, key, counter,
                            increment_counter, flags, flags_start, flags_end,
                            out);
#endif
}
