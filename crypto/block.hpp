#ifndef KUNLUN_CRYPTO_BLOCK_HPP_
#define KUNLUN_CRYPTO_BLOCK_HPP_

#include <immintrin.h>
#include <assert.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>

typedef __m128i block;

namespace Block{

// generate a block from two uint64_t values
__attribute__((target("sse2")))
inline block MakeBlock(uint64_t high, uint64_t low) {
	 return _mm_set_epi64x(high, low);
}

const block zero_block = _mm_set_epi64x(0, 0);
const block all_one_block = _mm_set_epi64x(0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF);
const block select_mask[2] = {zero_block, all_one_block};

// copy from https://devblogs.microsoft.com/oldnewthing/20141222-00/?p=43333
// Setting, clearing, and testing a single bit in an SSE register

block Calc2ToTheN(int N)
{
    // block zero_block = _mm_setzero_si128();
    // block all_one_block = _mm_cmpeq_epi32(zero, zero);
    block onesLowHigh = _mm_slli_epi64(all_one_block, 63);
    block single_one_block = N < 64 ? _mm_srli_si128(onesLowHigh, 64 / 8) : _mm_slli_si128(onesLowHigh, 64 / 8);
    return _mm_slli_epi64(single_one_block, N & 63);
}

block SetBitN(block value, int N)
{
    return _mm_or_si128(value, Calc2ToTheN(N));
}

block ClearBitN(block value, int N)
{
    return _mm_andnot_si128(value, Calc2ToTheN(N));
}


inline std::vector<block> XOR(std::vector<block> &vec_a, std::vector<block> &vec_b) 
{
    if(vec_a.size()!=vec_b.size()){
        std::cerr << "XORBlocks: size does not match" << std::endl;
    }
    size_t LEN = vec_a.size();

	std::vector<block> vec_result(LEN); 
    for (auto i = 0; i < LEN; i++){
        vec_result[i] = vec_a[i] ^ vec_b[i];
    }
    return std::move(vec_result);
}

inline std::vector<block> FixXOR(std::vector<block> &vec_a, block &b) 
{
    size_t LEN = vec_a.size();
    std::vector<block> vec_result(LEN); 
    for (auto i = 0; i < LEN; i++){
        vec_result[i] = vec_a[i] ^ b;
    }
    return std::move(vec_result); 
}


__attribute__((target("sse4")))
inline bool Compare(std::vector<block> &vec_a, std::vector<block> &vec_b, size_t LEN) 
{
	bool EQUAL = true;
    for (auto i = 0; i < LEN; i++) 
    {
        __m128i vcmp = _mm_xor_si128(vec_a[i], vec_b[i]); 
	    if(!_mm_testz_si128(vcmp, vcmp)){
            //std::cerr <<"blocks differ at position: "<< i << std::endl;
            EQUAL = false;
        }
	}
	return EQUAL;
}

inline bool Compare(const block &a, const block &b) 
{
    __m128i vcmp = _mm_xor_si128(a, b); 
    if(!_mm_testz_si128(vcmp, vcmp)) return false;
    else return true;
}

inline std::string ToString(const block &var)
{
    std::string str(16, '0'); 
    memcpy(&str[0], &var, 16);
    return std::move(str); 
}

// shrink 128*n bits into n block
inline void FromSparseBits(const uint8_t *bool_data, size_t BIT_LEN, block *block_data,  size_t BLOCK_LEN) 
{
    if(BIT_LEN != BLOCK_LEN*128){
        std::cerr << "BitsToBlocks: size does not match" << std::endl; 
    }

    for(auto i = 0; i < BLOCK_LEN; i++){ 
        block_data[i] = zero_block; 
        for(auto j = 0; j < 128; j++)
            if(bool_data[128*i+j] == 1){ 
                SetBitN(block_data[i], j); 
            }    
    } 
}

// shrink 128*n bits into n block
inline void FromDenseBits(const uint8_t *bool_data, size_t BIT_LEN, block *block_data,  size_t BLOCK_LEN) 
{
    if(BIT_LEN != BLOCK_LEN*128){
        std::cerr << "BitsToBlocks: size does not match" << std::endl; 
    }
    memcpy(block_data, bool_data, BIT_LEN/8); 
}

// expand n block to 128*n bits stored in dense form 
inline void ToDenseBits(const block *block_data, size_t BLOCK_LEN, uint8_t *bool_data, size_t BIT_LEN) 
{
    if(BIT_LEN != BLOCK_LEN*128){
        std::cerr << "BlocksToBits: size does not match" << std::endl; 
    }

    memcpy(bool_data, block_data, BIT_LEN/8); 
}

}


class BlockHash{
public:
    size_t operator()(const block& a) const
    {
        return std::hash<std::string>{}(Block::ToString(a));
    }
};


inline std::ostream& operator<<(std::ostream &out, const block &a) 
{
    out << std::hex;
    uint64_t* data = (uint64_t*)&a;

    out << std::setw(16) << std::setfill('0') << data[1]
        << std::setw(16) << std::setfill('0') << data[0];

    out << std::dec << std::setw(0);
    return out;
}


// copy from https://github.com/mischasan/sse2/blob/master/ssebmx.c
#define INP(x,y) inp[(x)*ncols/8 + (y)/8]
#define OUT(x,y) out[(y)*nrows/8 + (x)/8]

//#define II (i)
#define II (i ^ 7)

void BitMatrixTranspose(uint8_t const *inp, int nrows, int ncols, uint8_t *out)
{
// II is defined as either (i) or (i ^ 7)
    int rr, cc, i, h;
    union { __m128i x; uint8_t b[16]; } tmp;

    // Do the main body in [16 x 8] blocks:
    for (rr = 0; rr <= nrows - 16; rr += 16)
        for (cc = 0; cc < ncols; cc += 8) {
            for (i = 0; i < 16; ++i)
                tmp.b[i] = INP(rr + II, cc);
            for (i = 8; i--; tmp.x = _mm_slli_epi64(tmp.x, 1))
                *(uint16_t*)&OUT(rr, cc + II) = _mm_movemask_epi8(tmp.x);
        }

    if (rr == nrows) return;

    // The remainder is a row of [8 x 16]* [8 x 8]?

    //  Do the [8 x 16] blocks:
    for (cc = 0; cc <= ncols - 16; cc += 16) {
        for (i = 8; i--;)
            tmp.b[i] = h = *(uint16_t const*)&INP(rr + II, cc),
            tmp.b[i + 8] = h >> 8;
        for (i = 8; i--; tmp.x = _mm_slli_epi64(tmp.x, 1))
            OUT(rr, cc + II) = h = _mm_movemask_epi8(tmp.x),
            OUT(rr, cc + II + 8) = h >> 8;
    }

    if (cc == ncols) return;

    //  Do the remaining [8 x 8] block:
    for (i = 8; i--;)
        tmp.b[i] = INP(rr + II, cc);
    for (i = 8; i--; tmp.x = _mm_slli_epi64(tmp.x, 1))
        OUT(rr, cc + II) = _mm_movemask_epi8(tmp.x);
}




// // expand n block to 128*n bits: each bit store in a byte 
// inline void SparseBlocksToBits(const block *block_data, size_t BLOCK_LEN, uint8_t *bool_data, size_t BIT_LEN) 
// {
//     if(BIT_LEN != BLOCK_LEN*128){
//         std::cerr << "BlocksToBits: size does not match" << std::endl; 
//     }

//     for(auto i = 0; i < BLOCK_LEN; i++){ 
//         for(auto j = 0; j < 128; j++){
//             bool_data[i*128+j] = GetBlockBit(block_data[i], 127-j);
//         }
//     }   
// }


/* Linear orthomorphism function
 * [REF] Implementation of "Efficient and Secure Multiparty Computation from Fixed-Key Block Ciphers"
 * https://eprint.iacr.org/2019/074.pdf
 */

// __attribute__((target("sse2")))
// inline block Sigma(block a) {
//      return _mm_shuffle_epi32(a, 78) ^ (a & MakeBlock(0xFFFFFFFFFFFFFFFF, 0x00));
// }



// // set a __m128i mask: only the ith bit is 1
// inline block GenMaskBlock(size_t i)
// {
//     block mask; 
//     // if(i < 64) mask = _mm_set_epi64x(0L, 1ULL<<i);
//     // else mask = _mm_set_epi64x(1ULL<<(i-64), 0L);

//     if(i < 64) mask = _mm_set_epi64x(1ULL<<(63-i), 0L);
//     else mask = _mm_set_epi64x(0L, 1ULL<<(127-i));
//     return mask; 
// }

// inline bool GetLSB(const block &a) {
//    return (a[0] & 1) == 1;
// }

// Modified from
// https://mischasan.wordpress.com/2011/10/03/the-full-sse2-bit-matrix-transpose-routine/
// with inner most loops changed to _mm_set_epi8 and _mm_set_epi16
#define INPUT(x, y) input[(x)*COLUMN_NUM/8 + (y)/8]
#define OUTPUT(x, y) output[(y)*ROW_NUM/8 + (x)/8]

__attribute__((target("sse2")))
inline void empBitMatrixTranspose(uint8_t const *input, uint64_t ROW_NUM, uint64_t COLUMN_NUM, uint8_t *output) 
{
    int rr, cc, i, h;
    union { __m128i x; uint8_t b[16];} tmp;
    __m128i vec;
    assert(ROW_NUM%8 == 0 && COLUMN_NUM%8 == 0);

    // Do the main body in 16x8 blocks:
    for (rr = 0; rr <= ROW_NUM - 16; rr += 16) {
        for (cc = 0; cc < COLUMN_NUM; cc += 8) {
            vec = _mm_set_epi8(INPUT(rr + 15, cc), INPUT(rr + 14, cc), INPUT(rr + 13, cc),
                               INPUT(rr + 12, cc), INPUT(rr + 11, cc), INPUT(rr + 10, cc),
                               INPUT(rr + 9, cc),  INPUT(rr + 8, cc),  INPUT(rr + 7, cc),
                               INPUT(rr + 6, cc),  INPUT(rr + 5, cc),  INPUT(rr + 4, cc),
                               INPUT(rr + 3, cc),  INPUT(rr + 2, cc),  INPUT(rr + 1, cc),
                               INPUT(rr + 0, cc));
            for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1))
              *(uint16_t *)&OUTPUT(rr, cc + i) = _mm_movemask_epi8(vec);
        }
    }
    if (rr == ROW_NUM) return;

    // The remainder is a block of 8x(16n+8) bits (n may be 0).
    //  Do a PAIR of 8x8 blocks in each step:
    if ((COLUMN_NUM%8 == 0 && COLUMN_NUM%16 != 0) || (ROW_NUM%8 == 0 && ROW_NUM%16 != 0)) {
        // The fancy optimizations in the else-branch don't work if the above if-condition
        // holds, so we use the simpler non-simd variant for that case.
        for (cc = 0; cc <= COLUMN_NUM - 16; cc += 16) {
            for (i = 0; i < 8; ++i) {
                tmp.b[i] = h = *(uint16_t const *)&INPUT(rr + i, cc);
                tmp.b[i + 8] = h >> 8;
            }
            for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1)) {
                OUTPUT(rr, cc + i) = h = _mm_movemask_epi8(tmp.x);
                OUTPUT(rr, cc + i + 8) = h >> 8;
            }
        }
    } 
    else {
        for (cc = 0; cc <= COLUMN_NUM - 16; cc += 16) {
            vec = _mm_set_epi16(*(uint16_t const *)&INPUT(rr + 7, cc),
                                *(uint16_t const *)&INPUT(rr + 6, cc),
                                *(uint16_t const *)&INPUT(rr + 5, cc),
                                *(uint16_t const *)&INPUT(rr + 4, cc),
                                *(uint16_t const *)&INPUT(rr + 3, cc),
                                *(uint16_t const *)&INPUT(rr + 2, cc),
                                *(uint16_t const *)&INPUT(rr + 1, cc),
                                *(uint16_t const *)&INPUT(rr + 0, cc));
            for (i = 8; --i >= 0; vec = _mm_slli_epi64(vec, 1)) {
                OUTPUT(rr, cc + i) = h = _mm_movemask_epi8(vec);
                OUTPUT(rr, cc + i + 8) = h >> 8;
            }
        }
    }
    if (cc == COLUMN_NUM) return;

    //  Do the remaining 8x8 block:
    for (i = 0; i < 8; ++i)
        tmp.b[i] = INPUT(rr + i, cc);
    for (i = 8; --i >= 0; tmp.x = _mm_slli_epi64(tmp.x, 1))
        OUTPUT(rr, cc + i) = _mm_movemask_epi8(tmp.x);
}

// inline void SetBlockBit(block &a, size_t i) 
// {
//     block mask = GenMaskBlock(i); 
//     a = mask | a; 
// }

// // return 0 or 1
// inline uint8_t GetBlockBit(const block &a, size_t i)
// {
//     block mask = GenMaskBlock(i); 
//     mask = mask & a; 
//     return uint8_t(_mm_testz_si128(mask, mask));
// }

#endif
