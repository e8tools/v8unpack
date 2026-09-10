/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/
#include "V8File.h"
#include <iostream>
#include "zlib.h"
#include <boost/filesystem/fstream.hpp>

#define CHUNK 16384
#ifndef DEF_MEM_LEVEL
#  if MAX_MEM_LEVEL >= 8
#    define DEF_MEM_LEVEL 8
#  else
#    define DEF_MEM_LEVEL  MAX_MEM_LEVEL
#  endif
#endif

namespace v8unpack {

template<typename T, int length>
T hex_to_int(const char *hextext)
{
	auto s = hextext;
	auto i = length;
	T value = 0;
	for (;i; i--, s++) {

		auto lower_s = tolower(*s);
		if (lower_s >= '0' && lower_s <= '9') {
			value <<= 4;
			value += lower_s - '0';
		}
		else if (lower_s >= 'a' && lower_s <= 'f') {
			value <<= 4;
			value += lower_s - 'a' + 10;
		}
		else
			break;
	}
	return value;
}

uint32_t _httoi(const char *value)
{
	return hex_to_int<uint32_t, 8>(value);
}

uint64_t _httoi64(const char *value)
{
	return hex_to_int<uint64_t, 16>(value);
}

static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

template<typename T, int N>
void int_to_hex(T value, char *buf)
{
	for (int i = 2*N; i; i--) {
		buf[i - 1] = hex[value & 0xf];
		value >>= 4;
	}
}

void _itoht(uint32_t value, char *ht)
{
	int_to_hex<uint32_t, 4>(value, ht);
}

void _itoht64(uint64_t value, char *ht)
{
	int_to_hex<uint64_t, 8>(value, ht);
}


int Inflate(const std::string &in_filename, const std::string &out_filename)
{
	int ret;

	std::shared_ptr<std::istream> input;

	if (in_filename == "-") {

		// считываем стандартный ввод
		input.reset(&std::cin, [](...){});

	} else {

		boost::filesystem::path inf(in_filename);
		input.reset(new boost::filesystem::ifstream(inf, std::ios_base::binary));

		if (!*input) {
			return V8UNPACK_DEFLATE_IN_FILE_NOT_FOUND;
		}

	}

	std::shared_ptr<std::ostream> output;

	if (out_filename == "-") {

		// Выводим в стандартный вывод
		output.reset(&std::cout, [](...){});

	} else {

		boost::filesystem::path ouf(out_filename);
		output.reset(new boost::filesystem::ofstream (ouf, std::ios_base::binary));

		if (!*output) {
			return V8UNPACK_INFLATE_OUT_FILE_NOT_CREATED;
		}
	}

	try_inflate(*input, *output);

	return V8UNPACK_OK;
}

int Deflate(const std::string &in_filename, const std::string &out_filename)
{
	int ret;

	std::shared_ptr<std::istream> input;

	if (in_filename == "-") {

		// считываем стандартный ввод
		input.reset(&std::cin, [](...){});

	} else {

		boost::filesystem::path inf(in_filename);
		input.reset(new boost::filesystem::ifstream(inf, std::ios_base::binary));

		if (!*input) {
			return V8UNPACK_DEFLATE_IN_FILE_NOT_FOUND;
		}

	}

	std::shared_ptr<std::ostream> output;

	if (out_filename == "-") {

		// Выводим в стандартый вывод
		output.reset(&std::cout, [](...){});

	} else {

		boost::filesystem::path ouf(out_filename);
		output.reset(new boost::filesystem::ofstream (ouf, std::ios_base::binary));

		if (!*output) {
			return V8UNPACK_INFLATE_OUT_FILE_NOT_CREATED;
		}
	}

	ret = Deflate(*input, *output);

	if (ret)
		return V8UNPACK_DEFLATE_ERROR;

	return 0;
}

int Deflate(std::istream &source, const std::string &out_filename)
{
	std::ofstream dest(out_filename, std::ios_base::binary);
	return Deflate(source, dest);
}

int Deflate(std::istream &source, std::ostream &dest)
{

	int ret, flush;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	// allocate deflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;

	ret = deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);

	if (ret != Z_OK)
		return ret;

	// compress until end of file
	do {
		strm.avail_in = source.read(reinterpret_cast<char *>(in), CHUNK).gcount();
		if (source.bad()) {
			(void)deflateEnd(&strm);
			return Z_ERRNO;
		}

		flush = source.eof() ? Z_FINISH : Z_NO_FLUSH;
		strm.next_in = in;

		// run deflate() on input until output buffer not full, finish
		//   compression if all of source has been read in
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = deflate(&strm, flush);    // no bad return value
			assert(ret != Z_STREAM_ERROR);  // state not clobbered
			have = CHUNK - strm.avail_out;

			dest.write(reinterpret_cast<char *>(out), have);

			if (dest.bad()) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		} while (strm.avail_out == 0);
		assert(strm.avail_in == 0);     // all input will be used

		// done when last data in file processed
	} while (flush != Z_FINISH);
	assert(ret == Z_STREAM_END);        // stream will be complete

	// clean up and return
	(void)deflateEnd(&strm);
	return Z_OK;

}
int Inflate(std::istream &source, std::ostream &dest)
{
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char in[CHUNK];
	unsigned char out[CHUNK];

	// allocate inflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;

	ret = inflateInit2(&strm, -MAX_WBITS);
	if (ret != Z_OK)
		return ret;

	do {
		strm.avail_in = source.read(reinterpret_cast<char *>(in), CHUNK).gcount();
		if (source.bad()) {
			(void)inflateEnd(&strm);
			return Z_ERRNO;
		}
		if (strm.avail_in == 0)
			break;

		strm.next_in = in;

		// run inflate() on input until output buffer not full
		do {
			strm.avail_out = CHUNK;
			strm.next_out = out;
			ret = inflate(&strm, Z_NO_FLUSH);
			assert(ret != Z_STREAM_ERROR);  // state not clobbered
			switch (ret) {
				case Z_NEED_DICT:
					ret = Z_DATA_ERROR;     // and fall through
				case Z_DATA_ERROR:
				case Z_MEM_ERROR:
					(void)inflateEnd(&strm);
					return ret;
			}
			have = CHUNK - strm.avail_out;
			dest.write(reinterpret_cast<char *>(out), have);
			if (dest.bad()) {
				(void)inflateEnd(&strm);
				return Z_ERRNO;
			}
		} while (strm.avail_out == 0);

		// done when inflate() says it's done
	} while (ret != Z_STREAM_END);

	// clean up and return
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int Inflate(const char* in_buf, char** out_buf, uint32_t in_len, uint32_t* out_len)
{
	int ret;
	unsigned have;
	z_stream strm;
	unsigned char out[CHUNK];

	unsigned long out_buf_len = in_len + CHUNK;
	*out_buf = static_cast<char*> (realloc(*out_buf, out_buf_len));
	*out_len = 0;


	// allocate inflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm, -MAX_WBITS);
	if (ret != Z_OK)
		return ret;

	strm.avail_in = in_len;
	strm.next_in = (unsigned char *)(in_buf);

	// run inflate() on input until output buffer not full
	do {
		strm.avail_out = CHUNK;
		strm.next_out = out;
		ret = inflate(&strm, Z_NO_FLUSH);
		assert(ret != Z_STREAM_ERROR);  // state not clobbered
		switch (ret) {
			case Z_NEED_DICT:
				ret = Z_DATA_ERROR;     // and fall through
			case Z_DATA_ERROR:
			case Z_MEM_ERROR:
				(void)inflateEnd(&strm);
				return ret;
		}
		have = CHUNK - strm.avail_out;
		if (*out_len + have > out_buf_len) {
			//if (have < sizeof
			out_buf_len = out_buf_len + sizeof(out);
			*out_buf = static_cast<char*> (realloc(*out_buf, out_buf_len));
			if (!out_buf) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		}
		memcpy((*out_buf + *out_len), out, have);
		*out_len += have;
	} while (strm.avail_out == 0);

	// done when inflate() says it's done

	// clean up and return
	(void)inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

int Deflate(const char* in_buf, char** out_buf, uint32_t in_len, uint32_t* out_len)
{
	int ret, flush;
	unsigned have;
	z_stream strm;
	unsigned char out[CHUNK];

	unsigned long out_buf_len = in_len + CHUNK;
	*out_buf = static_cast<char*> (realloc(*out_buf, out_buf_len));
	*out_len = 0;

	// allocate deflate state
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	ret = deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY);

	if (ret != Z_OK)
		return ret;


	flush = Z_FINISH;
	strm.next_in = (unsigned char *)(in_buf);
	strm.avail_in = in_len;

	// run deflate() on input until output buffer not full, finish
	//   compression if all of source has been read in
	do {
		strm.avail_out = sizeof(out);
		strm.next_out = out;
		ret = deflate(&strm, flush);    // no bad return value
		assert(ret != Z_STREAM_ERROR);  // state not clobbered
		have = sizeof(out) - strm.avail_out;
		if (*out_len + have > out_buf_len) {
			//if (have < sizeof
			out_buf_len = out_buf_len + sizeof(out);
			*out_buf = static_cast<char*> (realloc(*out_buf, out_buf_len));
			if (!out_buf) {
				(void)deflateEnd(&strm);
				return Z_ERRNO;
			}
		}
		memcpy((*out_buf + *out_len), out, have);
		*out_len += have;
	} while (strm.avail_out == 0);
	assert(strm.avail_in == 0);     // all input will be used

	assert(ret == Z_STREAM_END);        // stream will be complete


	// clean up and return
	(void)deflateEnd(&strm);
	return Z_OK;

}

bool try_inflate(std::vector<char> &data)
{
	char    *inflated_data = nullptr;
	uint32_t inflated_data_size = 0;

	auto ret = Inflate(data.data(), &inflated_data, data.size(), &inflated_data_size);
	if (ret == Z_OK) {
		data.assign(inflated_data, inflated_data + inflated_data_size);
		free(inflated_data);
		return true;
	}
	if (inflated_data != nullptr) {
		free(inflated_data);
	}
	return false;
}

bool
try_inflate(std::istream &source, std::ostream &dest)
{
	auto gpos = source.tellg();
	auto ppos = dest.tellp();

	auto ret = Inflate(source, dest);

	if (ret != Z_OK) {
		// Файл не распаковывается - записываем, как есть
		source.clear();
		dest.clear();
		source.seekg(gpos, std::ios_base::beg);
		dest.seekp(ppos, std::ios_base::beg);

		full_copy(source, dest);

		return false;
	}

	return true;
}

bool
try_inflate(const boost::filesystem::path &source, const boost::filesystem::path &dest)
{
	boost::filesystem::ifstream inf(source, std::ios_base::binary);
	boost::filesystem::ofstream out(dest, std::ios_base::binary);

	return try_inflate(inf, out);
}


}