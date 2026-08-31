/*
    ========================================================

        Quark Utils Module
        By Quark Engine Development Team

    --------------------------------------------------------

    This file contains:
        * Data compression (DEFLATE via zlib)
        * Base64 encoding and decoding
        * Hashing utilities (CRC32, MD5, SHA1, SHA256)

    ========================================================
*/

#ifndef __QUARK_UTILS__
#define __QUARK_UTILS__

/**
 * @brief Compress data (DEFLATE algorithm).
 * @param data Data to compress.
 * @param dataSize Size of the input data, in bytes.
 * @param compDataSize Output compressed size, in bytes.
 * @return Compressed data (allocate via MemAlloc(), release with MemFree()).
 */
QCAPI unsigned char* CompressData(const unsigned char* data, int dataSize, int* compDataSize);

/**
 * @brief Decompress data (DEFLATE algorithm).
 * @param compData Compressed data to decompress.
 * @param compDataSize Size of the compressed data, in bytes.
 * @param dataSize Output decompressed size, in bytes.
 * @return Decompressed data (allocate via MemAlloc(), release with MemFree()).
 */
QCAPI unsigned char* DecompressData(const unsigned char* compData, int compDataSize, int* dataSize);

/**
 * @brief Encode data to a Base64 string (includes NULL terminator).
 * @param data Data to encode.
 * @param dataSize Size of the input data, in bytes.
 * @param outputSize Output string size, in bytes.
 * @return Encoded Base64 string (allocate via MemAlloc(), release with MemFree()).
 */
QCAPI char* EncodeDataBase64(const unsigned char* data, int dataSize, int* outputSize);

/**
 * @brief Decode a Base64 string (expected NULL terminated).
 * @param text Base64 string to decode.
 * @param outputSize Output decoded size, in bytes.
 * @return Decoded data (allocate via MemAlloc(), release with MemFree()).
 */
QCAPI unsigned char* DecodeDataBase64(const char* text, int* outputSize);

/**
 * @brief Compute CRC32 hash code.
 * @param data Data buffer.
 * @param dataSize Size of the data buffer, in bytes.
 * @return CRC32 hash value.
 */
QCAPI unsigned int ComputeCRC32(const unsigned char* data, int dataSize);

/**
 * @brief Compute MD5 hash code, returns pointer to static unsigned int[4] (16 bytes).
 * @param data Data buffer.
 * @param dataSize Size of the data buffer, in bytes.
 * @return Pointer to the hash value.
 */
QCAPI unsigned int* ComputeMD5(const unsigned char* data, int dataSize);

/**
 * @brief Compute SHA1 hash code, returns pointer to static unsigned int[5] (20 bytes).
 * @param data Data buffer.
 * @param dataSize Size of the data buffer, in bytes.
 * @return Pointer to the hash value.
 */
QCAPI unsigned int* ComputeSHA1(const unsigned char* data, int dataSize);

/**
 * @brief Compute SHA256 hash code, returns pointer to static unsigned int[8] (32 bytes).
 * @param data Data buffer.
 * @param dataSize Size of the data buffer, in bytes.
 * @return Pointer to the hash value.
 */
QCAPI unsigned int* ComputeSHA256(const unsigned char* data, int dataSize);

#endif // __QUARK_UTILS__
