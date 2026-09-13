/*----------------------------------------------------------
This Source Code Form is subject to the terms of the
Mozilla Public License, v.2.0. If a copy of the MPL
was not distributed with this file, You can obtain one
at http://mozilla.org/MPL/2.0/.
----------------------------------------------------------*/
/////////////////////////////////////////////////////////////////////////////
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
/////////////////////////////////////////////////////////////////////////////
/**
    2014-2021       dmpas       sergey(dot)batanov(at)dmpas(dot)ru
    2019-2020       fishca      fishcaroot(at)gmail(dot)com
 */

#pragma ide diagnostic ignored "misc-no-recursion"

#include "V8File.h"
#include "VersionFile.h"
#include <iostream>
#include <sstream>
#include <iterator>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <utility>
#include <memory>
#include <boost/filesystem/fstream.hpp>

namespace v8unpack {

using namespace std;

int RecursiveUnpack(
		const string                &directory,
		      basic_istream<char>   &file,
		const vector<string>        &filter,
		      bool                   boolInflate,
		      bool                   UnpackWhenNeed
);

CV8File::CV8File()
{
    IsDataPacked = true;
}

CV8File::CV8File(const CV8File &src)
    : FileHeader(src.FileHeader), IsDataPacked(src.IsDataPacked)
{
    ElemsAddrs.assign(src.ElemsAddrs.begin(), src.ElemsAddrs.end());
    Elems.assign(src.Elems.begin(), src.Elems.end());
}

CV8Elem::CV8Elem(const string &name)
{
	auto HeaderSize = CV8Elem::stElemHeaderBegin::Size() + name.size() * 2 + 4; // последние четыре всегда нули?
	resizeHeader(HeaderSize);
	memset(header.data(), 0, HeaderSize);

	SetName(name);
}

string CV8Elem::GetName() const
{
	auto ElemNameLen = (header.size() - CV8Elem::stElemHeaderBegin::Size()) / 2;
	stringstream ss;

	auto currentChar = header.data() + CV8Elem::stElemHeaderBegin::Size();
	for (auto j = 0; j < ElemNameLen * 2; j += 2, currentChar += 2) {
		if (*currentChar == '\0') {
			break;
		}
		ss << *currentChar;
	}

	return ss.str();
}

void CV8Elem::resizeHeader(size_t newSize)
{
	header.resize(newSize, 0);
}

int CV8Elem::SetName(const string &ElemName)
{
	uint32_t pos = CV8Elem::stElemHeaderBegin::Size();

	for (uint32_t j = 0; j < ElemName.size() * 2; j += 2, pos += 2) {
		header[pos] = ElemName[j / 2];
		header[pos + 1] = 0;
	}

	return 0;
}

void CV8Elem::Dispose()
{
	IsV8File = false;
}

template<typename format>
static size_t
ReadBlockData(basic_istream<char> &file, const typename format::block_header_t &firstBlockHeader, char *pBlockData)
{
	auto data_size = firstBlockHeader.data_size();
	auto Header = firstBlockHeader;
	auto pBlockHeader = &Header;

	uint32_t read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();
		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		file.read(&pBlockData[read_in_bytes], bytes_to_read);

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) { // есть следующая страница
			file.seekg(next_page_addr + format::BASE_OFFSET, ios_base::beg);
			file.read((char*)&Header, Header.Size());
		}
		else
			break;
	}

	return data_size;
}

template<typename format>
static size_t
ReadBlockData(basic_istream<char> &file, const typename format::block_header_t &firstBlockHeader, vector<char> &out)
{
	auto data_size = firstBlockHeader.data_size();
	out.resize(data_size);
	auto pBlockData = out.data();
	auto Header = firstBlockHeader;
	auto pBlockHeader = &Header;

	uint32_t read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();
		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		file.read(&pBlockData[read_in_bytes], bytes_to_read);

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) { // есть следующая страница
			file.seekg(next_page_addr + format::BASE_OFFSET, ios_base::beg);
			file.read((char*)&Header, Header.Size());
		}
		else
			break;
	}

	return data_size;
}

template<typename format>
static size_t
ReadBlockData(basic_istream<char> &file, const typename format::block_header_t &firstBlockHeader, basic_ostream<char> &out)
{
	uint32_t read_in_bytes;

	auto data_size = firstBlockHeader.data_size();
	auto Header = firstBlockHeader;
	auto pBlockHeader = &Header;

	const int buf_size = 1024; // TODO: Настраиваемый размер буфера
	char *pBlockData = new char[buf_size];

	read_in_bytes = 0;
	while (read_in_bytes < data_size) {

		auto page_size = pBlockHeader->page_size();
		auto next_page_addr = pBlockHeader->next_page_addr();
		auto bytes_to_read = MIN(page_size, data_size - read_in_bytes);

		uint32_t read_done = 0;
		while (read_done < bytes_to_read) {
			file.read(pBlockData, MIN(buf_size, bytes_to_read - read_done));
			uint32_t rd = file.gcount();
			out.write(pBlockData, rd);
			read_done += rd;
		}

		read_in_bytes += bytes_to_read;

		if (next_page_addr != format::UNDEFINED_VALUE) { // есть следующая страница
			file.seekg(next_page_addr + format::BASE_OFFSET, ios_base::beg);
			file.read((char*)&Header, Header.Size());
		}
		else
			break;
	}

	delete[] pBlockData;

	return V8UNPACK_OK;
}

template<typename format>
static size_t
DumpBlockData(basic_istream<char> &file, const typename format::block_header_t &firstBlockHeader, const boost::filesystem::path &path)
{
	boost::filesystem::ofstream out(path, ios_base::binary);
	return ReadBlockData<format>(file, firstBlockHeader, out);
}


template<typename format, typename in_stream_t, typename out_stream_t>
static size_t
ReadBlockData(in_stream_t &file, out_stream_t &out)
{
	typename format::block_header_t firstBlockHeader;
	file.read((char*)&firstBlockHeader, firstBlockHeader.Size());
	return ReadBlockData<format>(file, firstBlockHeader, out);
}

template<typename format, typename element_t, typename in_stream_t>
static vector<element_t>
ReadVector(in_stream_t &file)
{
	typename format::block_header_t firstBlockHeader;
	file.read((char*)&firstBlockHeader, firstBlockHeader.Size());
	auto elements_count = firstBlockHeader.data_size() / sizeof(element_t);

	vector<element_t> result;
	result.reserve(elements_count + 1);
	result.resize(elements_count);

	auto bytes_read = ReadBlockData<format>(file, firstBlockHeader, reinterpret_cast<char*>(result.data()));
	result.resize(bytes_read / sizeof(element_t));

	return result;
}

template<typename format, typename in_stream_t>
static vector<typename format::elem_addr_t>
ReadElementsAllocationTable(in_stream_t &file)
{
	return ReadVector<format, typename format::elem_addr_t, in_stream_t>(file);
}

template<typename format, typename in_stream_t, typename out_stream_t>
static bool
SafeReadBlockData(in_stream_t &file, out_stream_t &out, size_t &data_size)
{
	typename format::block_header_t firstBlockHeader;
	file.read((char*)&firstBlockHeader, firstBlockHeader.Size());
	if (!firstBlockHeader.IsCorrect()) {
		return false;
	}
	data_size = ReadBlockData<format>(file, firstBlockHeader, out);
	return true;
}

template<typename format, typename in_stream_t, typename out_stream_t>
static bool
SafeReadBlockData(in_stream_t &file, out_stream_t &out)
{
	size_t data_size;
	return SafeReadBlockData<format>(file, out, data_size);
}

template<typename format>
static
int SaveBlockDataToBuffer(char *&cur_pos, const char *pBlockData, uint32_t BlockDataSize, uint32_t PageSize = format::DEFAULT_PAGE_SIZE)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	typename format::block_header_t CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize, format::UNDEFINED_VALUE);
	memcpy(cur_pos, (char*)&CurBlockHeader, format::block_header_t::Size());
	cur_pos += format::block_header_t::Size();

	memcpy(cur_pos, pBlockData, BlockDataSize);
	cur_pos += BlockDataSize;

	for(uint32_t i = 0; i < PageSize - BlockDataSize; i++) {
		*cur_pos = 0;
		++cur_pos;
	}

	return 0;
}

template<typename format>
int SaveBlockData(basic_ostream<char> &file_out, const vector<char> &data, size_t PageSize = format::DEFAULT_PAGE_SIZE)
{
	auto BlockDataSize = data.size();
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	auto CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize);
	file_out.write(reinterpret_cast<char *>(&CurBlockHeader), CurBlockHeader.Size());
	file_out.write(data.data(), BlockDataSize);

	if (PageSize > BlockDataSize) {
		for (auto i = PageSize - BlockDataSize; i; i--) {
			file_out << '\0';
		}
	}

	return V8UNPACK_OK;
}

template<typename format>
int SaveBlockData(basic_ostream<char> &file_out, basic_istream<char> &file_in, size_t BlockDataSize, size_t PageSize = format::DEFAULT_PAGE_SIZE)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	auto CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize);
	file_out.write(reinterpret_cast<char *>(&CurBlockHeader), CurBlockHeader.Size());
	full_copy(file_in, file_out);

	if (PageSize > BlockDataSize) {
		for (auto i = PageSize - BlockDataSize; i; i--) {
			file_out << '\0';
		}
	}

	return V8UNPACK_OK;
}

template<typename format>
int SaveBlockData(basic_ostream<char> &file_out, boost::filesystem::path &in_file_path)
{
	auto BlockDataSize = boost::filesystem::file_size(in_file_path);
	auto PageSize = BlockDataSize;

	boost::filesystem::ifstream file_in(in_file_path, std::ios_base::binary);

	return SaveBlockData<format>(file_out, file_in, BlockDataSize, PageSize);
}

template<typename format>
int SaveBlockData(basic_ostream<char> &file_out, const char *pBlockData, size_t BlockDataSize, size_t PageSize = format::DEFAULT_PAGE_SIZE)
{
	if (PageSize < BlockDataSize)
		PageSize = BlockDataSize;

	auto CurBlockHeader = format::block_header_t::create(BlockDataSize, PageSize);
	file_out.write(reinterpret_cast<char *>(&CurBlockHeader), CurBlockHeader.Size());
	file_out.write(reinterpret_cast<const char *>(pBlockData), BlockDataSize);

	if (PageSize > BlockDataSize) {
		for (auto i = PageSize - BlockDataSize; i; i--) {
			file_out << '\0';
		}
	}

	return V8UNPACK_OK;
}

static int
directory_container_compatibility(const string &in_dirname)
{
	{ // copyinfo как признак внешней обработки / внешнего отчета - с ними упаковываемся по-старому всегда
		auto copyinfo_file_path = boost::filesystem::path(in_dirname) / "copyinfo";
		if (boost::filesystem::exists(copyinfo_file_path)) {
			return VersionFile::COMPATIBILITY_DEFAULT;
		}
	}
	{ // распакованный файл version (после Parse)
		auto version_file_path = boost::filesystem::path(in_dirname) / "version";
		if (boost::filesystem::exists(version_file_path)) {
			boost::filesystem::ifstream version_in(version_file_path);
			auto v = VersionFile::parse(version_in);
			return v.compatibility();
		}
	}
	{ // нераспакованный файл version (после Unpack)
		auto version_file_path = boost::filesystem::path(in_dirname) / "version.data";
		if (boost::filesystem::exists(version_file_path)) {
			boost::filesystem::ifstream version_in(version_file_path);
			std::stringstream contentStream;
			try_inflate(version_in, contentStream);
			contentStream.seekg(0);
			auto v = VersionFile::parse(contentStream);
			return v.compatibility();
		}
	}
	return VersionFile::COMPATIBILITY_DEFAULT;
}

void CV8File::Dispose()
{
	vector<CV8Elem>::iterator elem;
	for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
		elem->Dispose();
	}
	Elems.clear();
}

// Нѣкоторый условный предѣл
const size_t SmartLimit = 00 *1024;
const size_t SmartUnpackedLimit = 20 *1024*1024;

/*
	Лучше всѣго сжимается текст
	Берём степень сжатія текста в 99% (объём распакованных данных в 100 раз больше)
	Берём примѣрный порог использованія памяти в 20МБ (в этот объём должы влезть распакованные данные)
	Дѣлим 20МБ на 100 и получаем 200 КБ
	Упакованные данные размѣром до 200 КБ можно спокойно обрабатывать в памяти

	В дальнейшем этот показатель всё же будет вынесен в параметр командной строки
*/

class data_source_t
{
public:
	virtual istream &stream() = 0;
	virtual void save_as(const boost::filesystem::path &dest) = 0;
	virtual ~data_source_t() = default;
};

class temp_file_data_source_t : public data_source_t
{
public:
	explicit temp_file_data_source_t(const boost::filesystem::path &name) :
		path(name),
		file(name, ios_base::binary)
		{}

	istream &stream() override { return file; }

	void save_as(const boost::filesystem::path &dest) override
	{
		file.close();
		boost::system::error_code error;
		boost::filesystem::rename(path, dest, error);
	}

	~temp_file_data_source_t() override
	{
		if (file) {
			file.close();
			boost::system::error_code ec;
			boost::filesystem::remove(path, ec);
		}
	}
private:
	boost::filesystem::path path;
	boost::filesystem::ifstream file;
};

class vector_data_source_t : public data_source_t
{
public:
	explicit vector_data_source_t(vector<char> data) :
			__data(move(data)),
			__stream(__data.data(), __data.size())
	{}

	istream &stream() override { return __stream; }

	void save_as(const boost::filesystem::path &dest) override
	{
		__stream.seekg(0);
		boost::filesystem::ofstream out(dest, ios_base::binary);
		full_copy(__stream, out);
	}

	~vector_data_source_t() override = default;

private:
	vector<char> __data;
	boost::iostreams::stream<boost::iostreams::array_source> __stream;
};

template<typename format>
unique_ptr<data_source_t>
prepare_smart_source(basic_istream<char> &file, bool NeedUnpack, boost::filesystem::path &elem_path)
{
	typename format::block_header_t header;

	file.read((char*)&header, header.Size());
	auto data_size = header.data_size();

	if (NeedUnpack && data_size < SmartLimit) {
		vector<char> source_data;
		ReadBlockData<format>(file, header, source_data);
		try_inflate(source_data);

		return make_unique<vector_data_source_t>(source_data);
	}

	auto tmp_path = elem_path.parent_path() / ".v8unpack.tmp";
	DumpBlockData<format>(file, header, tmp_path);

	if (NeedUnpack) {
		auto inf_path = elem_path.parent_path() / ".v8unpack.inf";
		try_inflate(tmp_path, inf_path);
		boost::filesystem::remove(tmp_path);

		return make_unique<temp_file_data_source_t>(inf_path);
	}

	return make_unique<temp_file_data_source_t>(tmp_path);
}

template<typename format>
int SmartUnpack(basic_istream<char> &file, bool NeedUnpack, boost::filesystem::path &elem_path)
{
	auto src = prepare_smart_source<format>(file, NeedUnpack, elem_path);
	auto unpack_result = RecursiveUnpack(elem_path.string(), src->stream(), {}, false, false);
	if (unpack_result != V8UNPACK_OK) {
		src->save_as(elem_path);
	}
	return V8UNPACK_OK;
}

static bool NameInFilter(const string &name, const vector<string> &filter)
{
	return filter.empty()
		|| find(filter.begin(), filter.end(), name) != filter.end();
}

template<typename format>
static int recursive_unpack(const string& directory, basic_istream<char>& file, const vector<string>& filter, bool boolInflate, bool UnpackWhenNeed)
{
	int ret = 0;

	boost::filesystem::path p_dir(directory);

	if (!boost::filesystem::exists(p_dir)) {
		if (!boost::filesystem::create_directory(directory)) {
			cerr << "RecursiveUnpack. Error in creating directory!" << endl;
			return ret;
		}
	}

	typename format::file_header_t FileHeader;

	ifstream::pos_type offset = format::BASE_OFFSET;
	file.seekg(offset);
	file.read((char*)& FileHeader, FileHeader.Size());

	auto pElemsAddrs = ReadElementsAllocationTable<format>(file);
	auto ElemsNum = pElemsAddrs.size();

	for (uint32_t i = 0; i < ElemsNum; i++) {

		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}
		if (pElemsAddrs[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, ios_base::beg);

		CV8Elem elem;

		if (!SafeReadBlockData<format>(file, elem.header)) {
			ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			break;
		}
		string ElemName = elem.GetName();

		if (!NameInFilter(ElemName, filter)) {
			continue;
		}

		boost::filesystem::path elem_path= boost::filesystem::absolute(p_dir / ElemName);

		//080228 Блока данных может не быть, тогда адрес блока данных равен 0xffffffffffffffff
		if (pElemsAddrs[i].elem_data_addr != format::UNDEFINED_VALUE) {
			file.seekg(pElemsAddrs[i].elem_data_addr + format::BASE_OFFSET, ios_base::beg);
			SmartUnpack<format>(file, boolInflate, elem_path);
		}

	} // for i = ..ElemsNum

	return ret;
}

template<typename format>
static int list_files(boost::filesystem::ifstream &file)
{
	typename format::file_header_t FileHeader;

	file.seekg(format::BASE_OFFSET);
	file.read((char*)&FileHeader, FileHeader.Size());

	auto pElemsAddrs = ReadElementsAllocationTable<format>(file);
	auto ElemsNum = pElemsAddrs.size();

	for (uint32_t i = 0; i < ElemsNum; i++) {
		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}
		if (pElemsAddrs[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, ios_base::beg);

		CV8Elem elem;

		if (!SafeReadBlockData<format>(file, elem.header)) {
			continue;
		}

		cout << elem.GetName() << endl;
	}

	return V8UNPACK_OK;
}

int ListFiles(const string &filename)
{
	boost::filesystem::ifstream file(filename, ios_base::binary);

	if (!file) {
		cerr << "ListFiles `" << filename << "`. Input file not found!" << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	if (IsV8File16ZeroBased(file)) {
		return list_files<Format16ZeroBased>(file);
	}
	
	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return list_files<Format16>(file);
	}

	return list_files<Format15>(file);
}

static bool
MatchMask(const string &name, const string &mask)
{
	size_t n = 0;
	size_t m = 0;
	size_t star_n = string::npos;
	size_t star_m = string::npos;

	while (n < name.size()) {
		if (m < mask.size() && (mask[m] == '?' || mask[m] == name[n])) {
			++n;
			++m;
		} else if (m < mask.size() && mask[m] == '*') {
			star_n = n;
			star_m = m;
			++m;
		} else if (star_m != string::npos) {
			++star_n;
			n = star_n;
			m = star_m + 1;
		} else {
			return false;
		}
	}

	while (m < mask.size() && mask[m] == '*') {
		++m;
	}

	return m == mask.size();
}

static bool
NameMatchesMasks(const string &name, const vector<string> &masks)
{
	for (const auto &mask : masks) {
		if (MatchMask(name, mask)) {
			return true;
		}
	}
	return false;
}

template<typename format, typename stream_t, typename addr_t>
static bool append_block_to_free_list(
		stream_t &file,
		addr_t block_addr,
		addr_t &free_head)
{
	if (block_addr == format::UNDEFINED_VALUE) {
		return true;
	}

	auto current = block_addr;
	typename format::block_header_t header;

	for (;;) {
		file.clear();
		file.seekg(current + format::BASE_OFFSET, ios_base::beg);
		file.read(reinterpret_cast<char*>(&header), header.Size());
		if (!file || !header.IsCorrect()) {
			return false;
		}

		auto next = header.next_page_addr();
		if (next == format::UNDEFINED_VALUE) {
			break;
		}
		current = next;
	}

	header.set_next_page_addr(free_head);
	file.clear();
	file.seekp(current + format::BASE_OFFSET, ios_base::beg);
	file.write(reinterpret_cast<const char*>(&header), header.Size());
	if (!file) {
		return false;
	}

	free_head = block_addr;
	return true;
}

template<typename format, typename stream_t>
static bool write_into_block(
		stream_t &file,
		std::streamoff block_pos,
		size_t offset_in_data,
		const char *src,
		size_t len)
{
	if (len == 0) {
		return true;
	}

	const size_t end = offset_in_data + len;
	size_t data_cursor = 0;
	std::streamoff page_pos = block_pos;

	for (;;) {
		file.clear();
		file.seekg(page_pos, ios_base::beg);
		typename format::block_header_t header;
		file.read(reinterpret_cast<char*>(&header), header.Size());
		if (!file || !header.IsCorrect()) {
			return false;
		}

		const auto page_size = header.page_size();
		const auto next = header.next_page_addr();
		const std::streamoff page_data = page_pos + static_cast<std::streamoff>(header.Size());
		const size_t page_end = data_cursor + static_cast<size_t>(page_size);

		if (offset_in_data < page_end && end > data_cursor) {
			const size_t from = MAX(offset_in_data, data_cursor);
			const size_t to = MIN(end, page_end);
			const size_t chunk = to - from;
			file.clear();
			file.seekp(page_data + static_cast<std::streamoff>(from - data_cursor), ios_base::beg);
			file.write(src + (from - offset_in_data), chunk);
			if (!file) {
				return false;
			}
		}

		if (end <= page_end) {
			return true;
		}
		if (next == format::UNDEFINED_VALUE) {
			return false;
		}

		data_cursor = page_end;
		page_pos = static_cast<std::streamoff>(next + format::BASE_OFFSET);
	}
}

template<typename format>
static int delete_blocks(boost::filesystem::fstream &file, const string &filename, const vector<string> &masks)
{
	typename format::file_header_t FileHeader;
	file.seekg(format::BASE_OFFSET, ios_base::beg);
	file.read(reinterpret_cast<char*>(&FileHeader), FileHeader.Size());
	if (!file) {
		return V8UNPACK_NOT_V8_FILE;
	}

	const auto toc_pos = format::BASE_OFFSET + static_cast<std::streamoff>(format::file_header_t::Size());
	auto pElemsAddrs = ReadElementsAllocationTable<format>(file);
	auto ElemsNum = pElemsAddrs.size();

	auto free_head = FileHeader.next_page_addr;
	size_t removed = 0;

	for (uint32_t i = 0; i < ElemsNum; i++) {
		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			break;
		}
		if (pElemsAddrs[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.clear();
		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, ios_base::beg);

		CV8Elem elem;
		if (!SafeReadBlockData<format>(file, elem.header)) {
			return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
		}

		if (!NameMatchesMasks(elem.GetName(), masks)) {
			continue;
		}

		if (!append_block_to_free_list<format>(file, pElemsAddrs[i].elem_header_addr, free_head)) {
			return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
		}
		if (!append_block_to_free_list<format>(file, pElemsAddrs[i].elem_data_addr, free_head)) {
			return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
		}

		pElemsAddrs[i].elem_header_addr = format::UNDEFINED_VALUE;
		pElemsAddrs[i].elem_data_addr = format::UNDEFINED_VALUE;

		if (!write_into_block<format>(
				file,
				toc_pos,
				static_cast<size_t>(i) * format::elem_addr_t::Size(),
				reinterpret_cast<const char*>(&pElemsAddrs[i]),
				format::elem_addr_t::Size())) {
			return V8UNPACK_ERROR;
		}

		++removed;
	}

	if (removed != 0) {
		FileHeader.next_page_addr = free_head;
		file.clear();
		file.seekp(format::BASE_OFFSET, ios_base::beg);
		file.write(reinterpret_cast<const char*>(&FileHeader), FileHeader.Size());
		if (!file) {
			return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
		}
	}

	cout << "Delete `" << filename << "`: ok" << endl << flush;
	return V8UNPACK_OK;
}

int DeleteBlocks(const string &filename, const vector<string> &masks)
{
	boost::filesystem::fstream file(filename, ios_base::in | ios_base::out | ios_base::binary);

	if (!file) {
		cerr << "Delete `" << filename << "`. Input file not found!" << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	if (IsV8File16ZeroBased(file)) {
		return delete_blocks<Format16ZeroBased>(file, filename, masks);
	}

	if (!IsV8File(file)) {
		cerr << "Delete `" << filename << "` is not V8 file!" << endl;
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return delete_blocks<Format16>(file, filename, masks);
	}

	return delete_blocks<Format15>(file, filename, masks);
}

static string
elem_name_from_source(const string &source, const string &explicit_name)
{
	if (!explicit_name.empty()) {
		return explicit_name;
	}
	return boost::filesystem::path(source).filename().string();
}

static int
read_all_bytes(istream &in, vector<char> &data)
{
	data.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
	return V8UNPACK_OK;
}

static int
read_file_bytes(const string &path, vector<char> &data)
{
	boost::filesystem::ifstream in(path, ios_base::binary);
	if (!in) {
		cerr << "Add. Source not found: " << path << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}
	return read_all_bytes(in, data);
}

static int
prepare_add_payload(AddMode mode, const AddItem &item, vector<char> &header, vector<char> &data)
{
	const bool from_stdin = item.source == "-";
	const string name = elem_name_from_source(item.source, item.name);
	if (name.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	CV8Elem elem(name);
	const bool deflate = (mode == AddMode::Build);

	if (from_stdin) {
		read_all_bytes(cin, elem.data);
		if (deflate) {
			int ret = elem.Pack(true);
			if (ret) {
				return ret;
			}
		}
		header = elem.header;
		data = elem.data;
		return V8UNPACK_OK;
	}

	if (!boost::filesystem::exists(item.source)) {
		cerr << "Add. Source not found: " << item.source << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	if (boost::filesystem::is_directory(item.source)) {
		if (mode == AddMode::Pack) {
			cerr << "Add. Directory source requires -BUILD" << endl;
			return V8UNPACK_SHOW_USAGE;
		}
		elem.IsV8File = true;
		elem.UnpackedData.LoadFileFromFolder(item.source);
		int ret = elem.Pack(deflate);
		if (ret) {
			return ret;
		}
		header = elem.header;
		data = elem.data;
		return V8UNPACK_OK;
	}

	int ret = read_file_bytes(item.source, elem.data);
	if (ret != V8UNPACK_OK) {
		return ret;
	}
	if (deflate) {
		ret = elem.Pack(true);
		if (ret) {
			return ret;
		}
	}
	header = elem.header;
	data = elem.data;
	return V8UNPACK_OK;
}

template<typename format, typename stream_t, typename addr_t>
static bool take_free_page(
		stream_t &file,
		addr_t &free_head,
		size_t min_page_size,
		addr_t &out_addr,
		typename format::block_header_t &out_header)
{
	addr_t prev = format::UNDEFINED_VALUE;
	addr_t curr = free_head;

	while (curr != format::UNDEFINED_VALUE) {
		file.clear();
		file.seekg(curr + format::BASE_OFFSET, ios_base::beg);
		typename format::block_header_t header;
		file.read(reinterpret_cast<char*>(&header), header.Size());
		if (!file || !header.IsCorrect()) {
			return false;
		}

		const auto next = header.next_page_addr();
		if (header.page_size() >= min_page_size) {
			if (prev == format::UNDEFINED_VALUE) {
				free_head = next;
			} else {
				file.clear();
				file.seekg(prev + format::BASE_OFFSET, ios_base::beg);
				typename format::block_header_t prev_header;
				file.read(reinterpret_cast<char*>(&prev_header), prev_header.Size());
				if (!file || !prev_header.IsCorrect()) {
					return false;
				}
				prev_header.set_next_page_addr(next);
				file.clear();
				file.seekp(prev + format::BASE_OFFSET, ios_base::beg);
				file.write(reinterpret_cast<const char*>(&prev_header), prev_header.Size());
				if (!file) {
					return false;
				}
			}
			out_addr = curr;
			out_header = header;
			return true;
		}

		prev = curr;
		curr = next;
	}

	return false;
}

template<typename format, typename stream_t, typename addr_t>
static bool allocate_block(
		stream_t &file,
		addr_t &free_head,
		const char *data,
		size_t data_size,
		size_t preferred_page_size,
		addr_t &out_addr)
{
	size_t page_size = preferred_page_size;
	if (page_size < data_size) {
		page_size = data_size;
	}

	typename format::block_header_t reused;
	if (take_free_page<format>(file, free_head, data_size, out_addr, reused)) {
		reused.set_data_size(data_size);
		reused.set_next_page_addr(format::UNDEFINED_VALUE);
		file.clear();
		file.seekp(out_addr + format::BASE_OFFSET, ios_base::beg);
		file.write(reinterpret_cast<const char*>(&reused), reused.Size());
		if (data_size != 0) {
			file.write(data, data_size);
		}
		return static_cast<bool>(file);
	}

	file.clear();
	file.seekp(0, ios_base::end);
	const auto endpos = file.tellp();
	out_addr = static_cast<addr_t>(static_cast<std::streamoff>(endpos) - format::BASE_OFFSET);

	typename format::block_header_t header = format::block_header_t::create(
			static_cast<uint32_t>(data_size),
			static_cast<uint32_t>(page_size));
	file.write(reinterpret_cast<const char*>(&header), header.Size());
	if (data_size != 0) {
		file.write(data, data_size);
	}
	for (size_t i = page_size - data_size; i; --i) {
		file << '\0';
	}
	return static_cast<bool>(file);
}

template<typename format, typename stream_t>
static bool toc_last_page(
		stream_t &file,
		std::streamoff toc_pos,
		size_t &capacity,
		std::streamoff &last_pos,
		typename format::block_header_t &last_header)
{
	capacity = 0;
	std::streamoff pos = toc_pos;

	for (;;) {
		file.clear();
		file.seekg(pos, ios_base::beg);
		typename format::block_header_t header;
		file.read(reinterpret_cast<char*>(&header), header.Size());
		if (!file || !header.IsCorrect()) {
			return false;
		}
		capacity += static_cast<size_t>(header.page_size());
		last_pos = pos;
		last_header = header;
		if (header.next_page_addr() == format::UNDEFINED_VALUE) {
			return true;
		}
		pos = static_cast<std::streamoff>(header.next_page_addr() + format::BASE_OFFSET);
	}
}

template<typename format, typename stream_t, typename addr_t>
static bool ensure_toc_capacity(
		stream_t &file,
		std::streamoff toc_pos,
		size_t needed,
		addr_t &free_head)
{
	size_t capacity = 0;
	std::streamoff last_pos = toc_pos;
	typename format::block_header_t last_header;

	if (!toc_last_page<format>(file, toc_pos, capacity, last_pos, last_header)) {
		return false;
	}

	while (capacity < needed) {
		addr_t new_addr = 0;
		if (!allocate_block<format>(
				file,
				free_head,
				nullptr,
				0,
				format::DEFAULT_PAGE_SIZE,
				new_addr)) {
			return false;
		}

		last_header.set_next_page_addr(new_addr);
		file.clear();
		file.seekp(last_pos, ios_base::beg);
		file.write(reinterpret_cast<const char*>(&last_header), last_header.Size());
		if (!file) {
			return false;
		}

		capacity += format::DEFAULT_PAGE_SIZE;
		last_pos = static_cast<std::streamoff>(new_addr + format::BASE_OFFSET);
		file.clear();
		file.seekg(last_pos, ios_base::beg);
		file.read(reinterpret_cast<char*>(&last_header), last_header.Size());
		if (!file || !last_header.IsCorrect()) {
			return false;
		}
	}

	return true;
}

template<typename format>
static int find_unused_toc_index(const vector<typename format::elem_addr_t> &toc)
{
	for (size_t i = 0; i < toc.size(); i++) {
		if (toc[i].fffffff != format::UNDEFINED_VALUE) {
			break;
		}
		if (toc[i].elem_header_addr == format::UNDEFINED_VALUE) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

template<typename format, typename stream_t>
static int find_toc_index_by_name(
		stream_t &file,
		const vector<typename format::elem_addr_t> &toc,
		const string &name)
{
	for (size_t i = 0; i < toc.size(); i++) {
		if (toc[i].fffffff != format::UNDEFINED_VALUE) {
			break;
		}
		if (toc[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.clear();
		file.seekg(toc[i].elem_header_addr + format::BASE_OFFSET, ios_base::beg);
		CV8Elem elem;
		if (!SafeReadBlockData<format>(file, elem.header)) {
			return -2;
		}
		if (elem.GetName() == name) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

template<typename format>
static int add_items(
		boost::filesystem::fstream &file,
		const string &filename,
		const vector<AddItem> &items,
		AddMode mode,
		bool replace)
{
	typename format::file_header_t FileHeader;
	file.clear();
	file.seekg(format::BASE_OFFSET, ios_base::beg);
	file.read(reinterpret_cast<char*>(&FileHeader), FileHeader.Size());
	if (!file) {
		return V8UNPACK_NOT_V8_FILE;
	}

	const auto toc_pos = format::BASE_OFFSET + static_cast<std::streamoff>(format::file_header_t::Size());
	auto toc = ReadElementsAllocationTable<format>(file);
	auto free_head = FileHeader.next_page_addr;
	const char *op = replace ? "Put" : "Add";

	for (const auto &item : items) {
		const string name = elem_name_from_source(item.source, item.name);
		int existing = find_toc_index_by_name<format>(file, toc, name);
		if (existing == -2) {
			return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
		}
		if (existing >= 0 && !replace) {
			cerr << op << ". `" << name << "` already exists!" << endl;
			return V8UNPACK_ELEM_ALREADY_EXISTS;
		}

		vector<char> header;
		vector<char> data;
		int ret = prepare_add_payload(mode, item, header, data);
		if (ret != V8UNPACK_OK) {
			return ret;
		}

		if (existing >= 0) {
			if (!append_block_to_free_list<format>(file, toc[existing].elem_header_addr, free_head)) {
				return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			}
			if (!append_block_to_free_list<format>(file, toc[existing].elem_data_addr, free_head)) {
				return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			}
		}

		typename format::elem_addr_t entry;
		entry.fffffff = format::UNDEFINED_VALUE;
		if (!allocate_block<format>(
				file,
				free_head,
				header.data(),
				header.size(),
				header.size(),
				entry.elem_header_addr)) {
			return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
		}
		if (!allocate_block<format>(
				file,
				free_head,
				data.data(),
				data.size(),
				format::DEFAULT_PAGE_SIZE,
				entry.elem_data_addr)) {
			return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
		}

		size_t index;
		if (existing >= 0) {
			index = static_cast<size_t>(existing);
			toc[index] = entry;
		} else {
			int slot = find_unused_toc_index<format>(toc);
			if (slot >= 0) {
				index = static_cast<size_t>(slot);
				toc[index] = entry;
			} else {
				index = toc.size();
				const size_t new_size = (index + 1) * format::elem_addr_t::Size();
				if (!ensure_toc_capacity<format>(file, toc_pos, new_size, free_head)) {
					return V8UNPACK_ERROR;
				}

				file.clear();
				file.seekg(toc_pos, ios_base::beg);
				typename format::block_header_t toc_header;
				file.read(reinterpret_cast<char*>(&toc_header), toc_header.Size());
				if (!file || !toc_header.IsCorrect()) {
					return V8UNPACK_ERROR;
				}
				toc_header.set_data_size(static_cast<decltype(toc_header.data_size())>(new_size));
				file.clear();
				file.seekp(toc_pos, ios_base::beg);
				file.write(reinterpret_cast<const char*>(&toc_header), toc_header.Size());
				if (!file) {
					return V8UNPACK_ERROR;
				}

				toc.push_back(entry);
			}
		}

		if (!write_into_block<format>(
				file,
				toc_pos,
				index * format::elem_addr_t::Size(),
				reinterpret_cast<const char*>(&entry),
				format::elem_addr_t::Size())) {
			return V8UNPACK_ERROR;
		}
	}

	FileHeader.next_page_addr = free_head;
	file.clear();
	file.seekp(format::BASE_OFFSET, ios_base::beg);
	file.write(reinterpret_cast<const char*>(&FileHeader), FileHeader.Size());
	if (!file) {
		return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
	}

	cout << op << " `" << filename << "`: ok" << endl << flush;
	return V8UNPACK_OK;
}

int AddToContainer(const string &filename, const vector<AddItem> &items, AddMode mode, bool replace)
{
	if (items.empty()) {
		return V8UNPACK_SHOW_USAGE;
	}

	const char *op = replace ? "Put" : "Add";
	boost::filesystem::fstream file(filename, ios_base::in | ios_base::out | ios_base::binary);
	if (!file) {
		cerr << op << " `" << filename << "`. Input file not found!" << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	if (IsV8File16ZeroBased(file)) {
		return add_items<Format16ZeroBased>(file, filename, items, mode, replace);
	}

	if (!IsV8File(file)) {
		cerr << op << " `" << filename << "` is not V8 file!" << endl;
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return add_items<Format16>(file, filename, items, mode, replace);
	}

	return add_items<Format15>(file, filename, items, mode, replace);
}

template<typename format>
static int unpack_block_to_stream(basic_istream<char> &file, basic_ostream<char> &out, const string &block_name)
{
	file.seekg(format::BASE_OFFSET, ios_base::beg);
	typename format::file_header_t FileHeader;
	file.read(reinterpret_cast<char*>(&FileHeader), FileHeader.Size());

	auto pElemsAddrs = ReadElementsAllocationTable<format>(file);
	auto ElemsNum = pElemsAddrs.size();

	for (uint32_t i = 0; i < ElemsNum; i++) {
		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			break;
		}
		if (pElemsAddrs[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + format::BASE_OFFSET, ios_base::beg);

		CV8Elem elem;
		if (!SafeReadBlockData<format>(file, elem.header)) {
			return V8UNPACK_HEADER_ELEM_NOT_CORRECT;
		}

		if (elem.GetName() != block_name) {
			continue;
		}

		if (pElemsAddrs[i].elem_data_addr != format::UNDEFINED_VALUE) {
			file.seekg(pElemsAddrs[i].elem_data_addr + format::BASE_OFFSET, ios_base::beg);
			ReadBlockData<format>(file, out);
		}
		return V8UNPACK_OK;
	}

	cerr << "Unpack. Block `" << block_name << "` not found!" << endl;
	return V8UNPACK_ERROR;
}

template<typename format>
static int unpack_to_folder(boost::filesystem::ifstream &file, const string &dirname, const string &UnpackElemWithName, bool print_progress)
{
	if (dirname == "-" && !UnpackElemWithName.empty()) {
		return unpack_block_to_stream<format>(file, cout, UnpackElemWithName);
	}

	int ret = V8UNPACK_OK;

	boost::filesystem::path p_dir(dirname);

	if (!boost::filesystem::exists(p_dir)) {
		if (!boost::filesystem::create_directory(dirname)) {
			cerr << "RecursiveUnpack. Error in creating directory!" << endl;
			return ret;
		}
	}

	typename format::file_header_t FileHeader;

	ifstream::pos_type offset = format::BASE_OFFSET;
	file.seekg(offset);
	file.read((char*)&FileHeader, FileHeader.Size());

	if (UnpackElemWithName.empty()) {
		boost::filesystem::path filename_out(dirname);
		filename_out /= "FileHeader";
		boost::filesystem::ofstream file_out(filename_out, ios_base::binary);
		file_out.write((char*)&FileHeader, FileHeader.Size());
		file_out.close();
	}

	auto pElemsAddrs = ReadElementsAllocationTable<format>(file);
	auto ElemsNum = pElemsAddrs.size();

	for (uint32_t i = 0; i < ElemsNum; i++) {

		if (pElemsAddrs[i].fffffff != format::UNDEFINED_VALUE) {
			ElemsNum = i;
			break;
		}
		if (pElemsAddrs[i].elem_header_addr == format::UNDEFINED_VALUE) {
			continue;
		}

		file.seekg(pElemsAddrs[i].elem_header_addr + offset, ios_base::beg);

		CV8Elem elem;
		if (!SafeReadBlockData<format>(file, elem.header)) {
			ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
			break;
		}

		string ElemName = elem.GetName();

		// если передано имя блока для распаковки, пропускаем все остальные
		if (!UnpackElemWithName.empty() && UnpackElemWithName != ElemName) {
			continue;
		}

		boost::filesystem::ofstream header_out;
		header_out.open(p_dir / (ElemName + ".header"), ios_base::binary);
		if (!header_out) {
			cerr << "UnpackToFolder. Error in creating file!" << endl;
			return -1;
		}
		header_out.write(elem.header.data(), elem.header.size());
		header_out.close();

		boost::filesystem::ofstream data_out;
		data_out.open(p_dir / (ElemName + ".data"), ios_base::binary);
		if (!data_out) {
			cerr << "UnpackToFolder. Error in creating file!" << endl;
			return -1;
		}
		if (pElemsAddrs[i].elem_data_addr != format::UNDEFINED_VALUE) {
			file.seekg(pElemsAddrs[i].elem_data_addr + offset, ios_base::beg);
			ReadBlockData<format>(file, data_out);
		}
		data_out.close();
	}

	return V8UNPACK_OK;
}

int UnpackToFolder(const string &filename_in, const string &dirname, const string &UnpackElemWithName, bool print_progress)
{
	int ret = 0;

	boost::filesystem::ifstream file(filename_in, ios_base::binary);

	if (!file) {
		cerr << "UnpackToFolder. Input file not found!" << endl;
		return -1;
	}

	if (IsV8File16ZeroBased(file)) {
		return unpack_to_folder<Format16ZeroBased>(file, dirname, UnpackElemWithName, print_progress);
	}

	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return unpack_to_folder<Format16>(file, dirname, UnpackElemWithName, print_progress);
	}

	return unpack_to_folder<Format15>(file, dirname, UnpackElemWithName, print_progress);
}

template <typename format>
static bool checkV8File(basic_istream<char> &file)
{
	bool result = false;

	auto offset = file.tellg();
	file.seekg(0, file.end);
	auto file_size = file.tellg();

	typename format::file_header_t FileHeader;
	if (file_size >= format::BASE_OFFSET + FileHeader.Size()) {

		file.seekg(format::BASE_OFFSET);
		file.read((char *) &FileHeader, FileHeader.Size());

		typename format::block_header_t BlockHeader;
		if (file_size >= format::BASE_OFFSET + FileHeader.Size() + BlockHeader.Size()) {
			memset(&BlockHeader, 0, BlockHeader.Size());
			file.read((char *) &BlockHeader, BlockHeader.Size());
			result = BlockHeader.IsCorrect();
		} else {
			// Если в файле нет первого блока, значит адрес страницы должен быть UNDEFINED
			result = (FileHeader.next_page_addr == format::UNDEFINED_VALUE);
		}
	}
	file.seekg(offset);
	file.clear();

	return result;
}

bool IsV8File(basic_istream<char> &file)
{
	return checkV8File<Format15>(file);
}

bool IsV8File16ZeroBased(basic_istream<char>& file)
{
	return checkV8File <Format16ZeroBased>(file);
}

bool IsV8File16(basic_istream<char>& file)
{
	return checkV8File <Format16> (file);
}

struct PackElementEntry {
	boost::filesystem::path  header_file;
	boost::filesystem::path  data_file;
	size_t                   header_size;
	size_t                   data_size;
};

template<typename format>
static int
pack_from_folder(const boost::filesystem::path &p_curdir, boost::filesystem::ofstream &file_out)
{
	file_out << format::placeholder;
	{
		boost::filesystem::ifstream file_in(p_curdir / "FileHeader", ios_base::binary);
		full_copy(file_in, file_out);
	}

	boost::filesystem::directory_iterator d_end;
	boost::filesystem::directory_iterator it(p_curdir);

	vector<PackElementEntry> Elems;

	for (; it != d_end; it++) {
		boost::filesystem::path current_file(it->path());
		if (current_file.extension().string() == ".header") {

			PackElementEntry elem;

			elem.header_file = current_file;
			elem.header_size = boost::filesystem::file_size(current_file);

			elem.data_file = current_file.replace_extension(".data");
			elem.data_size = boost::filesystem::file_size(elem.data_file);

			Elems.push_back(elem);

		}
	} // for it

	auto ElemsNum = Elems.size();
	vector<typename format::elem_addr_t> ElemsAddrs;
	ElemsAddrs.reserve(ElemsNum);

	// cur_block_addr - смещение текущего блока
	// мы должны посчитать:
	//  + [0] заголовок файла
	//  + [1] заголовок блока с адресами
	//  + [2] размер самого блока адресов (не менее одной страницы?)
	//  + для каждого блока:
	//      + [3] заголовок блока метаданных (header)
	//      + [4] сами метаданные (header)
	//      + [5] заголовок данных
	//      + [6] сами данные (не менее одной страницы?)

	// [0] + [1]
	uint32_t cur_block_addr = format::file_header_t::Size() + format::block_header_t::Size();
	size_t addr_block_size = MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);
	cur_block_addr += addr_block_size; // +[2]

	for (const auto &elem : Elems) {
		typename format::elem_addr_t addr;

		addr.elem_header_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size() + elem.header_size; // +[3]+[4]

		addr.elem_data_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size(); // +[5]
		cur_block_addr += MAX(elem.data_size, format::DEFAULT_PAGE_SIZE); // +[6]

		addr.fffffff = format::UNDEFINED_VALUE;

		ElemsAddrs.push_back(addr);
	}

	SaveBlockData<format>(file_out, (char*) ElemsAddrs.data(), format::elem_addr_t::Size() * ElemsNum);

	for (const auto &elem : Elems) {

		boost::filesystem::ifstream header_in(elem.header_file, ios_base::binary);
		SaveBlockData<format>(file_out, header_in, elem.header_size, elem.header_size);

		boost::filesystem::ifstream data_in(elem.data_file, ios_base::binary);
		SaveBlockData<format>(file_out, data_in, elem.data_size, V8_DEFAULT_PAGE_SIZE);
	}

	file_out.close();

	return V8UNPACK_OK;
}

int PackFromFolder(const string &dirname, const string &filename_out)
{
	boost::filesystem::path p_curdir(dirname);
	boost::filesystem::ofstream file_out(filename_out, ios_base::binary);
	if (!file_out) {
		cerr << "SaveFile. Error in creating file: " << filename_out << endl;
		return -1;
	}

	int compatibility = directory_container_compatibility(dirname);
	if (compatibility >= VersionFile::COMPATIBILITY_V80316) {
		return pack_from_folder<Format16>(p_curdir, file_out);
	}

	return pack_from_folder<Format15>(p_curdir, file_out);
}

int RecursiveUnpack(const string &directory, basic_istream<char> &file, const vector<string> &filter, bool boolInflate, bool UnpackWhenNeed)
{
	if (IsV8File16ZeroBased(file)) {
		return recursive_unpack<Format16ZeroBased>(directory, file, filter, boolInflate, UnpackWhenNeed);
	}

	if (!IsV8File(file)) {
		return V8UNPACK_NOT_V8_FILE;
	}

	if (IsV8File16(file)) {
		return recursive_unpack<Format16>(directory, file, filter, boolInflate, UnpackWhenNeed);
	}

	return recursive_unpack<Format15>(directory, file, filter, boolInflate, UnpackWhenNeed);
}

int Parse(const string &filename_in, const string &dirname, const vector< string > &filter)
{
    int ret = 0;

    boost::filesystem::ifstream file_in(filename_in, ios_base::binary);

    if (!file_in) {
        cerr << "Parse. `" << filename_in << "` not found!" << endl;
        return -1;
    }

    ret = RecursiveUnpack(dirname, file_in, filter, true, false);

    if (ret == V8UNPACK_NOT_V8_FILE) {
        cerr << "Parse. `" << filename_in << "` is not V8 file!" << endl;
        return ret;
    }

    cout << "Parse `" << filename_in << "`: ok" << endl << flush;

    return ret;
}

int CV8File::LoadFileFromFolder(const string &dirname)
{
	typedef Format15 format;

    FileHeader.next_page_addr = format::UNDEFINED_VALUE;
    FileHeader.page_size = format::DEFAULT_PAGE_SIZE;
    FileHeader.storage_ver = 0;
    FileHeader.reserved = 0;

    Elems.clear();

    boost::filesystem::directory_iterator d_end;
    boost::filesystem::directory_iterator dit(dirname);

    for (; dit != d_end; ++dit) {
        boost::filesystem::path current_file(dit->path());
        if (current_file.filename().string().at(0) == '.')
            continue;

		CV8Elem elem(current_file.filename().string());

		if (boost::filesystem::is_directory(current_file)) {

			elem.IsV8File = true;

			elem.UnpackedData.LoadFileFromFolder(current_file.string());
			elem.Pack(false);

        } else {
            elem.IsV8File = false;

			elem.data.resize(boost::filesystem::file_size(current_file));

			boost::filesystem::ifstream file_in(current_file, ios_base::binary);
			file_in.read(elem.data.data(), elem.data.size());
        }

        Elems.push_back(elem);
    } // for directory_iterator

	return V8UNPACK_OK;
}

static bool
is_dot_file(const boost::filesystem::path &path)
{
	return path.filename().string() == "."
		|| path.filename().string() == "..";
}

template<typename format>
static int
recursive_pack(const string &in_dirname, const string &out_filename, bool dont_deflate)
{
	uint32_t ElemsNum = 0;
	{
		boost::filesystem::directory_iterator d_end;
		boost::filesystem::directory_iterator dit(in_dirname);

		for (; dit != d_end; ++dit) {
			if (!is_dot_file(dit->path())) {
				++ElemsNum;
			}
		}
	}

	typename format::file_header_t FileHeader;

	//Предварительные расчеты длины заголовка таблицы содержимого TOC файла
	FileHeader.next_page_addr = format::UNDEFINED_VALUE;
	FileHeader.page_size = format::DEFAULT_PAGE_SIZE;
	FileHeader.storage_ver = 0;
	FileHeader.reserved = 0;

	auto cur_block_addr = format::file_header_t::Size() + format::block_header_t::Size();
	typename format::elem_addr_t *pTOC;
	pTOC = new typename format::elem_addr_t[ElemsNum];
	cur_block_addr += MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	boost::filesystem::ofstream file_out(out_filename, ios_base::binary);
	//Открываем выходной файл контейнер на запись
	if (!file_out) {
		delete [] pTOC;
		cout << "SaveFile. Error in creating file!" << endl;
		return V8UNPACK_ERROR_CREATING_OUTPUT_FILE;
	}

	file_out << format::placeholder;

	//Резервируем место в начале файла под заголовок и TOC
	for(unsigned i=0; i < cur_block_addr; i++) {
		file_out << '\0';
	}

	uint32_t ElemNum = 0;

	boost::filesystem::directory_iterator d_end;
	boost::filesystem::directory_iterator dit(in_dirname);
	for (; dit != d_end; ++dit) {

		if (is_dot_file(dit->path())) {
			continue;
		}

		boost::filesystem::path current_file(dit->path());
		string name = current_file.filename().string();

		CV8Elem pElem(name);

		pTOC[ElemNum].elem_header_addr = file_out.tellp() - format::BASE_OFFSET;
		SaveBlockData<format>(file_out, pElem.header.data(), pElem.header.size(), pElem.header.size());

		pTOC[ElemNum].elem_data_addr = file_out.tellp() - format::BASE_OFFSET;
		pTOC[ElemNum].fffffff = format::UNDEFINED_VALUE;

		if (boost::filesystem::is_directory(current_file)) {

			pElem.IsV8File = true;

			pElem.UnpackedData.LoadFileFromFolder(current_file.string());
			pElem.Pack(!dont_deflate);

			SaveBlockData<format>(file_out, pElem.data.data(), pElem.data.size());

		} else {

			pElem.IsV8File = false;

			auto DataSize = boost::filesystem::file_size(current_file);

			boost::filesystem::path p_filename(in_dirname);
			p_filename /= name;
			boost::filesystem::ifstream file_in(p_filename, ios_base::binary);

			if (DataSize < SmartUnpackedLimit) {

				pElem.data.resize(DataSize);
				file_in.read(pElem.data.data(), pElem.data.size());
				pElem.Pack(!dont_deflate);
				SaveBlockData<format>(file_out, pElem.data);

			} else {

				if (dont_deflate) {
					SaveBlockData<format>(file_out, file_in, DataSize);
				} else {
					// Упаковка через промежуточный файл
					boost::filesystem::path tmp_file_path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

					Deflate(file_in, tmp_file_path.string());
					SaveBlockData<format>(file_out, tmp_file_path);

					boost::filesystem::remove(tmp_file_path);
				}
			}
		}

		ElemNum++;
	}

	//Записывем заголовок файла
	file_out.seekp(format::BASE_OFFSET, ios_base::beg);
	file_out.write(reinterpret_cast<const char*>(&FileHeader), format::file_header_t::Size());

	//Записываем блок TOC
	SaveBlockData<format>(file_out, (const char*) pTOC, format::elem_addr_t::Size() * ElemsNum);

	delete [] pTOC;

	cout << endl << "Build `" << out_filename << "` OK!" << endl << flush;

	return V8UNPACK_OK;
}

int BuildCfFile(const string &in_dirname, const string &out_filename, bool dont_deflate)
{
	//filename can't be empty
	if (in_dirname.empty()) {
		cerr << "Argument error - Set of `in_dirname' argument" << endl;
		return V8UNPACK_SHOW_USAGE;
	}

	if (out_filename.empty()) {
		cerr << "Argument error - Set of `out_filename' argument" << endl;
		return V8UNPACK_SHOW_USAGE;
	}

	if (!boost::filesystem::exists(in_dirname)) {
		cerr << "Source directory does not exist!" << endl;
		return V8UNPACK_SOURCE_DOES_NOT_EXIST;
	}

	int compatibility = directory_container_compatibility(in_dirname);

	if (compatibility >= VersionFile::COMPATIBILITY_V80316) {
		return recursive_pack<Format16>(in_dirname, out_filename, dont_deflate);
	}

	return recursive_pack<Format15>(in_dirname, out_filename, dont_deflate);
}

int CV8Elem::Pack(bool deflate)
{
	int ret = 0;
	if (!IsV8File) {

		if (deflate) {

			char *DeflateBuffer = nullptr;
			uint32_t DeflateSize = 0;

			ret = Deflate(data.data(), &DeflateBuffer, data.size(), &DeflateSize);
			if (ret) {
				return ret;
			}

			data.resize(DeflateSize);
			memcpy(data.data(), DeflateBuffer, DeflateSize);

			delete [] DeflateBuffer;
		}

	} else {

		UnpackedData.GetData(data);
		UnpackedData.Dispose();

		if (deflate) {

			char *DeflateBuffer = nullptr;
			uint32_t DeflateSize = 0;

			ret = Deflate(data.data(), &DeflateBuffer, data.size(), &DeflateSize);
			if (ret) {
				return ret;
			}

			data.resize(DeflateSize);
			memcpy(data.data(), DeflateBuffer, DeflateSize);

			delete [] DeflateBuffer;

		}

		IsV8File = false;
	}

	return V8UNPACK_OK;
}

int CV8File::GetData(vector<char> &data)
{
	typedef Format15 format;

	auto ElemsNum = Elems.size();

	// заголовок блока и данные блока - адреса элементов с учетом минимальной страницы 512 байт
	auto NeedDataBufferSize = format::file_header_t::Size()
			+ format::block_header_t::Size()
			+ MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	for (auto &elem : Elems) {

		// заголовок блока и данные блока - заголовок элемента
		NeedDataBufferSize += format::block_header_t::Size()  + elem.header.size();

		if (elem.IsV8File) {
			elem.UnpackedData.GetData(elem.data);
			elem.IsV8File = false;
		}
		NeedDataBufferSize += format::block_header_t::Size() + MAX(elem.data.size(), format::DEFAULT_PAGE_SIZE);
	}

	data.resize(NeedDataBufferSize);

	// Создаем и заполняем данные по адресам элементов
	vector<format::elem_addr_t> pTempElemsAddrs(ElemsNum);
	auto pCurrentTempElem = pTempElemsAddrs.begin();

	auto cur_block_addr = format::file_header_t::Size() + format::block_header_t::Size();
	cur_block_addr += MAX(format::elem_addr_t::Size() * ElemsNum, format::DEFAULT_PAGE_SIZE);

	for (const auto &elem : Elems) {

		pCurrentTempElem->elem_header_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size() + elem.header.size();

		pCurrentTempElem->elem_data_addr = cur_block_addr;
		cur_block_addr += format::block_header_t::Size();
		cur_block_addr += MAX(elem.data.size(), format::DEFAULT_PAGE_SIZE);

		pCurrentTempElem->fffffff = format::UNDEFINED_VALUE;
		++pCurrentTempElem;
	}

	char *cur_pos = data.data();

	// записываем заголовок
	memcpy(cur_pos, (char*) &FileHeader, format::file_header_t::Size());
	cur_pos += format::file_header_t::Size();

	// записываем адреса элементов
	SaveBlockDataToBuffer<format>(cur_pos, (char*) pTempElemsAddrs.data(), format::elem_addr_t::Size() * ElemsNum);

	// записываем элементы (заголовок и данные)
	for (auto elem : Elems) {
		SaveBlockDataToBuffer<format>(cur_pos, elem.header.data(), elem.header.size(), elem.header.size());
		SaveBlockDataToBuffer<format>(cur_pos, elem.data.data(), elem.data.size());
	}

	return V8UNPACK_OK;
}

stBlockHeader stBlockHeader::create(uint32_t block_data_size, uint32_t page_size)
{
	return create(block_data_size, page_size, UNDEFINED_VALUE);
}

stBlockHeader stBlockHeader::create(uint32_t block_data_size, uint32_t page_size, uint32_t next_page_addr)
{
	stBlockHeader BlockHeader;
	BlockHeader.set_data_size(block_data_size);
	BlockHeader.set_page_size(page_size);
	BlockHeader.set_next_page_addr(next_page_addr);
	return BlockHeader;
}

stBlockHeader64 stBlockHeader64::create(uint64_t block_data_size, uint64_t page_size)
{
	return create(block_data_size, page_size, UNDEFINED_VALUE);
}

stBlockHeader64 stBlockHeader64::create(uint64_t  block_data_size, uint64_t  page_size, uint64_t next_page_addr)
{
	stBlockHeader64 BlockHeader;
	BlockHeader.set_data_size(block_data_size);
	BlockHeader.set_page_size(page_size);
	BlockHeader.set_next_page_addr(next_page_addr);
	return BlockHeader;
}

}
#pragma clang diagnostic pop