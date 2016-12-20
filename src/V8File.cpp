/////////////////////////////////////////////////////////////////////////////
//
//
//	Author:			disa_da
//	E-mail:			disa_da2@mail.ru
//
//
/////////////////////////////////////////////////////////////////////////////

/**
    2014-2016       dmpas       sergey(dot)batanov(at)dmpas(dot)ru
 */

// V8File.cpp: implementation of the CV8File class.
//
//////////////////////////////////////////////////////////////////////


#include "V8File.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <iostream>

#ifndef MAX_PATH
#define MAX_PATH (260)
#endif


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

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


CV8File::CV8File(char *pFileData, bool boolInflate)
{
    LoadFile(pFileData, boolInflate);
}

CV8File::~CV8File()
{
}

CV8Elem::CV8Elem(const CV8Elem &src)
    : pHeader(src.pHeader), HeaderSize(src.HeaderSize),
      pData(src.pData), DataSize(src.DataSize),
      UnpackedData(src.UnpackedData), IsV8File(src.IsV8File),
      NeedUnpack(src.NeedUnpack)
{ }

CV8Elem::CV8Elem()
{

    IsV8File = false;
    HeaderSize = 0;
    DataSize = 0;
}

CV8Elem::~CV8Elem()
{
    // TODO: Добавить удаление данных
}



int CV8File::Inflate(const std::string &in_filename, const std::string &out_filename)
{
    int ret;

    boost::filesystem::path inf(in_filename);
    boost::filesystem::ifstream in_file(inf, std::ios_base::binary);

    if (!in_file)
        return V8UNPACK_INFLATE_IN_FILE_NOT_FOUND;

    boost::filesystem::path ouf(out_filename);
    boost::filesystem::ofstream out_file(ouf, std::ios_base::binary);

    if (!out_file)
        return V8UNPACK_INFLATE_OUT_FILE_NOT_CREATED;

    ret = Inflate(in_file, out_file);

    if (ret == Z_DATA_ERROR)
        return V8UNPACK_INFLATE_DATAERROR;
    if (ret)
        return V8UNPACK_INFLATE_ERROR;

    return 0;
}

int CV8File::Deflate(const std::string &in_filename, const std::string &out_filename)
{

    int ret;

    boost::filesystem::path inf(in_filename);
    boost::filesystem::ifstream in_file(inf, std::ios_base::binary);

    if (!in_file)
        return V8UNPACK_DEFLATE_IN_FILE_NOT_FOUND;

    boost::filesystem::path ouf(out_filename);
    boost::filesystem::ofstream out_file(ouf, std::ios_base::binary);

    if (!out_file)
        return V8UNPACK_DEFLATE_OUT_FILE_NOT_CREATED;

    ret = Deflate(in_file, out_file);

    if (ret)
        return V8UNPACK_DEFLATE_ERROR;

    return 0;
}

int CV8File::Deflate(std::basic_ifstream<char> &source, std::basic_ofstream<char> &dest)
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
int CV8File::Inflate(std::basic_ifstream<char> &source, std::basic_ofstream<char> &dest)
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


int CV8File::Inflate(const char* in_buf, char** out_buf, ULONG in_len, ULONG* out_len)
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

int CV8File::Deflate(const char* in_buf, char** out_buf, ULONG in_len, ULONG* out_len)
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

int CV8File::LoadFile(char *pFileData, ULONG FileDataSize, bool boolInflate, bool UnpackWhenNeed)
{
    int ret = 0;

    if (!pFileData) {
        return V8UNPACK_ERROR;
    }

    if (!IsV8File(pFileData, FileDataSize)) {
        return V8UNPACK_NOT_V8_FILE;
    }

    char *InflateBuffer = NULL;
    ULONG InflateSize = 0;

    stFileHeader *pFileHeader = (stFileHeader*) pFileData;

    stBlockHeader *pBlockHeader;

    pBlockHeader = (stBlockHeader*) &pFileHeader[1];
    memcpy(&FileHeader, pFileData, stFileHeader::Size());


    UINT ElemsAddrsSize;
    stElemAddr *pElemsAddrs = NULL;
    ReadBlockData(pFileData, pBlockHeader, (char*&)pElemsAddrs, &ElemsAddrsSize);


    unsigned int ElemsNum = ElemsAddrsSize / stElemAddr::Size();

    Elems.clear();

    for (UINT i = 0; i < ElemsNum; i++) {

        if (pElemsAddrs[i].fffffff != V8_FF_SIGNATURE) {
            ElemsNum = i;
            break;
        }

        pBlockHeader = (stBlockHeader*) &pFileData[pElemsAddrs[i].elem_header_addr];

        if (pBlockHeader->EOL_0D != 0x0d ||
                pBlockHeader->EOL_0A != 0x0a ||
                pBlockHeader->space1 != 0x20 ||
                pBlockHeader->space2 != 0x20 ||
                pBlockHeader->space3 != 0x20 ||
                pBlockHeader->EOL2_0D != 0x0d ||
                pBlockHeader->EOL2_0A != 0x0a) {

            ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
            break;
        }

        CV8Elem elem;
        ReadBlockData(pFileData, pBlockHeader, elem.pHeader, &elem.HeaderSize);


        //080228 Блока данных может не быть, тогда адрес блока данных равен 0x7fffffff
        if (pElemsAddrs[i].elem_data_addr != V8_FF_SIGNATURE) {
            pBlockHeader = (stBlockHeader*) &pFileData[pElemsAddrs[i].elem_data_addr];
            ReadBlockData(pFileData, pBlockHeader, elem.pData, &elem.DataSize);
        } else
            ReadBlockData(pFileData, NULL, elem.pData, &elem.DataSize);

        elem.UnpackedData.IsDataPacked = false;

        if (boolInflate && IsDataPacked) {
            ret = Inflate(elem.pData, &InflateBuffer, elem.DataSize, &InflateSize);

            if (ret)
                IsDataPacked = false;
            else {

                elem.NeedUnpack = false; // отложенная распаковка не нужна
                delete[] elem.pData; //нераспакованные данные больше не нужны
                elem.pData = NULL;
                if (IsV8File(InflateBuffer, InflateSize)) {
                    ret = elem.UnpackedData.LoadFile(InflateBuffer, InflateSize, boolInflate);
                    if (ret)
                        break;

                    elem.pData = NULL;
                    elem.IsV8File = true;
                } else {
                    elem.pData = new char[InflateSize];
                    elem.DataSize = InflateSize;
                    memcpy(elem.pData, InflateBuffer, InflateSize);
                }
                ret = 0;

            }
        }

        Elems.push_back(elem);

    } // for i = ..ElemsNum


    if (InflateBuffer)
        free(InflateBuffer);

	delete [] pElemsAddrs;

    return ret;
}

void CV8File::Dispose()
{
    std::vector<CV8Elem>::iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
        if (elem->pData)
            delete [] elem->pData;
        if (elem->pHeader)
            delete [] elem->pHeader;
    }
    Elems.clear();
}

//++ dmpas Затычка Issue6

// Нѣкоторый условный предѣл
const size_t SmartLimit = 100 *1024;

/*
    Лучше всѣго сжимается текст
    Берём степень сжатія текста в 99% (объём распакованных данных в 100 раз больше)
    Берём примѣрный порог использованія памяти в 10МБ (в этот объём должы влезть распакованные данные)
    Дѣлим 10МБ на 100 и получаем 100 КБ
    Упакованные данные размѣром до 100 КБ можно спокойно обрабатывать в памяти

    В дальнейшем этот показатель всё же будет вынесен в параметр командной строки
*/

int SmartUnpack(std::basic_ifstream<char> &file, bool NeedUnpack, boost::filesystem::path &elem_path)
{
    CV8File::stBlockHeader header;
    file.read((char*)&header, sizeof(header));

    int ret = 0;

    size_t data_size = CV8File::_httoi(header.data_size_hex);
    if (!NeedUnpack || data_size > SmartLimit) {
        /* 1) Имѣем дѣло с условно большими данными - работаем через промежуточный файл */
        /* 2) Не нужна распаковка - пишем прямо в файл-приёмник */

        boost::filesystem::ifstream src;

        boost::filesystem::path tmp_path = elem_path.parent_path() / ".v8unpack.tmp";
        boost::filesystem::path inf_path = elem_path.parent_path() / ".v8unpack.inf";
        boost::filesystem::path src_path;

        if (NeedUnpack) {
            /* Временный файл */

            boost::filesystem::ofstream out;

            out.open(tmp_path, std::ios_base::binary);
            UINT BlockDataSize = 0;
            CV8File::ReadBlockData(file, &header, out, &BlockDataSize);
            out.close();

            out.open(inf_path, std::ios_base::binary);
            boost::filesystem::ifstream inf(tmp_path, std::ios_base::binary);

            ret = CV8File::Inflate(inf, out);

            if (ret) {
                // Файл не распаковывается - записываем, как есть
                inf.seekg(0, std::ios_base::beg);

                do {

                    const int block_size = 4096;
                    char data[block_size]; // TODO: Оценить размер блока
                    int data_size = inf.read(data, block_size).gcount();

                    if (data_size != 0)
                        out.write(data, data_size);

                } while (inf);

            }

            inf.close();
            boost::filesystem::remove(tmp_path);
            out.close();

            src_path = inf_path;

        } else {
            /* Конечный файл */
            boost::filesystem::ofstream out;
            out.open(tmp_path, std::ios_base::binary);
            UINT BlockDataSize = 0;
            CV8File::ReadBlockData(file, &header, out, &BlockDataSize);
            out.close();

            src_path = tmp_path;
        }

        src.open(src_path, std::ios_base::binary);

        if (CV8File::IsV8File(src)) {
            CV8File elem;
            elem.UnpackToDirectoryNoLoad(elem_path.string(), src, data_size, false, false);
            src.close();
            elem.Dispose();
            boost::filesystem::remove(src_path);
        } else {
            src.close();
            boost::system::error_code error;
            boost::filesystem::rename(src_path, elem_path, error);
        }


    } else {

        /* Имѣем полное право помѣстить файл в память */

        UINT uDataSize;
        char *source_data = NULL;

        CV8File::ReadBlockData(file, &header, source_data, &uDataSize);

        char *out_data = NULL;
        ULONG out_data_size = 0;

        ret = CV8File::Inflate(source_data, &out_data, uDataSize, &out_data_size);
        if (ret) {

            // файл не распаковывается - записываем, как есть
            out_data = source_data;
            out_data_size = uDataSize;

            source_data = NULL;
        }

        delete [] source_data;

        if (CV8File::IsV8File(out_data, uDataSize)) {
            /* Это 8-файл - раскладываем его*/

            CV8File elem;
            elem.LoadFile(out_data, out_data_size, false, false);
            elem.SaveFileToFolder(elem_path.string());

            elem.Dispose();

        } else {
            /* Тупо пишем содержимое в цѣлевой файл*/

            boost::filesystem::ofstream out(elem_path, std::ios_base::binary);
            out.write(out_data, out_data_size);

        }

		free(out_data);

    }

    return ret;
}
//-- dmpas Затычка Issue6

int CV8File::UnpackToDirectoryNoLoad(const std::string &directory, std::basic_ifstream<char> &file, ULONG FileDataSize, bool boolInflate, bool UnpackWhenNeed)
{
    int ret = 0;

    if (!IsV8File(file)) {
        return V8UNPACK_NOT_V8_FILE;
    }

    boost::filesystem::path p_dir(directory);

    if (!boost::filesystem::exists(p_dir)) {
        if (!boost::filesystem::create_directory(directory)) {
            std::cerr << "UnpackToDirectoryNoLoad. Error in creating directory!" << std::endl;
            return ret;
        }
    }

    file.read((char*)&FileHeader, sizeof(FileHeader));

    stBlockHeader BlockHeader;
    stBlockHeader *pBlockHeader = &BlockHeader;

    file.read((char*)&BlockHeader, sizeof(BlockHeader));

    UINT ElemsAddrsSize;
    stElemAddr *pElemsAddrs = NULL;
    ReadBlockData(file, pBlockHeader, (char*&)pElemsAddrs, &ElemsAddrsSize);

    unsigned int ElemsNum = ElemsAddrsSize / stElemAddr::Size();

    Elems.clear();

    for (UINT i = 0; i < ElemsNum; i++) {

        if (pElemsAddrs[i].fffffff != V8_FF_SIGNATURE) {
            ElemsNum = i;
            break;
        }

        file.seekg(pElemsAddrs[i].elem_header_addr, std::ios_base::beg);
        file.read((char*)&BlockHeader, sizeof(BlockHeader));

        if (pBlockHeader->EOL_0D != 0x0d ||
                pBlockHeader->EOL_0A != 0x0a ||
                pBlockHeader->space1 != 0x20 ||
                pBlockHeader->space2 != 0x20 ||
                pBlockHeader->space3 != 0x20 ||
                pBlockHeader->EOL2_0D != 0x0d ||
                pBlockHeader->EOL2_0A != 0x0a) {

            ret = V8UNPACK_HEADER_ELEM_NOT_CORRECT;
            break;
        }

        CV8Elem elem;
        ReadBlockData(file, pBlockHeader, elem.pHeader, &elem.HeaderSize);

        char ElemName[512];
        UINT ElemNameLen;

        GetElemName(elem, ElemName, &ElemNameLen);

        boost::filesystem::path elem_path(p_dir / ElemName);
        elem_path = boost::filesystem::absolute(elem_path);

        //080228 Блока данных может не быть, тогда адрес блока данных равен 0x7fffffff
        if (pElemsAddrs[i].elem_data_addr != V8_FF_SIGNATURE) {
            file.seekg(pElemsAddrs[i].elem_data_addr, std::ios_base::beg);
            SmartUnpack(file, boolInflate && IsDataPacked, elem_path);
        } else {
            // TODO: Зачем это нужно??
            //ReadBlockData(file, NULL, o_tmp, &elem.DataSize);
        }

		delete [] elem.pHeader;

     } // for i = ..ElemsNum

	delete [] pElemsAddrs;

    return ret;
}

int CV8File::UnpackToFolder(const std::string &filename_in, const std::string &dirname, char *UnpackElemWithName, bool print_progress)
{
    char *pFileData = NULL;

    int ret = 0;

    boost::filesystem::path filepath(filename_in);
    boost::filesystem::ifstream file_in(filepath, std::ios_base::binary);

    if (!file_in) {
        std::cerr << "UnpackToFolder. Input file not found!" << std::endl;
        return -1;
    }

    file_in.seekg(0, std::ios_base::end);
    ULONG FileDataSize = file_in.tellg();
    file_in.seekg(0, std::ios_base::beg);

    pFileData = new char[FileDataSize];
    if (!pFileData) {
        std::cerr << "UnpackToFolder. Not enough memory!" << std::endl;
        return -1;
    }

    size_t sz_r = file_in.read(reinterpret_cast<char*>(pFileData), FileDataSize).gcount();
    if (sz_r != FileDataSize) {
        std::cerr << "UnpackToFolder. Error in reading file!" << std::endl;
        return sz_r;
    }
    file_in.close();

    ret = LoadFile(pFileData, FileDataSize, false);

    if (pFileData)
        delete pFileData;

    if (ret == V8UNPACK_NOT_V8_FILE) {
        std::cerr << "UnpackToFolder. This is not V8 file!" << std::endl;
        return ret;
    }
    if (ret == V8UNPACK_NOT_V8_FILE) {
        std::cerr << "UnpackToFolder. Error in load file in memory!" << std::endl;
        return ret;
    }

    ret = boost::filesystem::create_directory(dirname);
    if (ret && errno == ENOENT) {
        std::cerr << "UnpackToFolder. Error in creating directory!" << std::endl;
        return ret;
    }

    std::string filename_out = dirname;
    filename_out += "/FileHeader";

    boost::filesystem::ofstream file_out(filename_out, std::ios_base::binary);
    if (!file_out) {
        std::cerr << "UnpackToFolder. Error in creating file!" << std::endl;
        return ret;
    }
    file_out.write(reinterpret_cast<char *>(&FileHeader), stFileHeader::Size());
    file_out.close();

    char ElemName[512];
    UINT ElemNameLen;

    UINT one_percent = Elems.size() / 50;
    if (print_progress && one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    UINT ElemNum = 0;
    std::vector<CV8Elem>::const_iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
        if (print_progress && ElemNum && one_percent && ElemNum%one_percent == 0) {
            if (ElemNum % (one_percent*10) == 0)
                std::cout << "|" << std::flush;
            else
                std::cout << "." << std::flush;
        }

        GetElemName(*elem, ElemName, &ElemNameLen);

        // если передано имя блока для распаковки, пропускаем все остальные
        if (UnpackElemWithName && strcmp(UnpackElemWithName, ElemName))
            continue;

        filename_out = dirname;
        filename_out += "/";
        filename_out += ElemName;
        filename_out += ".header";

        file_out.open(filename_out, std::ios_base::binary);
        if (!file_out) {
            std::cerr << "UnpackToFolder. Error in creating file!" << std::endl;
            return -1;
        }
        file_out.write(reinterpret_cast<char*>(elem->pHeader), elem->HeaderSize);
        file_out.close();

        filename_out = dirname;
        filename_out += "/";
        filename_out += ElemName;
        filename_out += ".data";

        file_out.open(filename_out, std::ios_base::binary);
        if (!file_out) {
            std::cerr << "UnpackToFolder. Error in creating file!" << std::endl;
            return -1;
        }
        file_out.write(reinterpret_cast<char *>(elem->pData), elem->DataSize);
        file_out.close();

        ++ElemNum;
    }


    if (print_progress && one_percent) {
        std::cout << std::endl;
    }

    return 0;
}

DWORD CV8File::_httoi(const char *value)
{

    DWORD result = 0;

    const char *s = value;
    unsigned char lower_s;
    while (*s != '\0' && *s != ' ') {
        lower_s = tolower(*s);
        if (lower_s >= '0' && lower_s <= '9') {
            result <<= 4;
            result += lower_s - '0';
        } else if (lower_s >= 'a' && lower_s <= 'f') {
            result <<= 4;
            result += lower_s - 'a' + 10;
        } else
            break;
        s++;
    }
    return result;
}

int CV8File::ReadBlockData(char *pFileData, stBlockHeader *pBlockHeader, char *&pBlockData, UINT *BlockDataSize)
{
    DWORD data_size, page_size, next_page_addr;
    UINT read_in_bytes, bytes_to_read;

    if (pBlockHeader != NULL) {
        data_size = _httoi(pBlockHeader->data_size_hex);
        pBlockData = new char[data_size];
        if (!pBlockData) {
            std::cerr << "ReadBlockData. BlockData == NULL." << std::endl;
            return -1;
        }
    } else
        data_size = 0;

    read_in_bytes = 0;
    while (read_in_bytes < data_size) {

        page_size = _httoi(pBlockHeader->page_size_hex);
        next_page_addr = _httoi(pBlockHeader->next_page_addr_hex);

        bytes_to_read = MIN(page_size, data_size - read_in_bytes);

        memcpy(&pBlockData[read_in_bytes], (char*)(&pBlockHeader[1]), bytes_to_read);

        read_in_bytes += bytes_to_read;

        if (next_page_addr != V8_FF_SIGNATURE) // есть следующая страница
            pBlockHeader = (stBlockHeader*) &pFileData[next_page_addr];
        else
            break;
    }

    if (BlockDataSize)
        *BlockDataSize = data_size;

    return 0;
}

int CV8File::ReadBlockData(std::basic_ifstream<char> &file, stBlockHeader *pBlockHeader, char *&pBlockData, UINT *BlockDataSize)
{
    DWORD data_size, page_size, next_page_addr;
    UINT read_in_bytes, bytes_to_read;

    stBlockHeader Header;
    if (pBlockHeader != NULL) {
        data_size = _httoi(pBlockHeader->data_size_hex);
        pBlockData = new char[data_size];
        if (!pBlockData) {
            std::cerr << "ReadBlockData. BlockData == NULL." << std::endl;
            return -1;
        }
        Header = *pBlockHeader;
        pBlockHeader = &Header;
    } else
        data_size = 0;

    read_in_bytes = 0;
    while (read_in_bytes < data_size) {

        page_size = _httoi(pBlockHeader->page_size_hex);
        next_page_addr = _httoi(pBlockHeader->next_page_addr_hex);

        bytes_to_read = MIN(page_size, data_size - read_in_bytes);

        file.read(&pBlockData[read_in_bytes], bytes_to_read);

        read_in_bytes += bytes_to_read;

        if (next_page_addr != V8_FF_SIGNATURE) { // есть следующая страница
            //pBlockHeader = (stBlockHeader*) &pFileData[next_page_addr];
            file.seekg(next_page_addr, std::ios_base::beg);
            file.read((char*)&Header, sizeof(Header));
        } else
            break;
    }

    if (BlockDataSize)
        *BlockDataSize = data_size;

    return 0;
}

int CV8File::ReadBlockData(std::basic_ifstream<char> &file, stBlockHeader *pBlockHeader, std::basic_ofstream<char> &out, UINT *BlockDataSize)
{
    DWORD data_size, page_size, next_page_addr;
    UINT read_in_bytes, bytes_to_read;

    stBlockHeader Header;
    if (pBlockHeader != NULL) {
        data_size = _httoi(pBlockHeader->data_size_hex);
        Header = *pBlockHeader;
        pBlockHeader = &Header;
    } else
        data_size = 0;

    read_in_bytes = 0;
    while (read_in_bytes < data_size) {

        page_size = _httoi(pBlockHeader->page_size_hex);
        next_page_addr = _httoi(pBlockHeader->next_page_addr_hex);

        bytes_to_read = MIN(page_size, data_size - read_in_bytes);

        const int buf_size = 1024; // TODO: Настраиваемый размер буфера
        char *pBlockData = new char [buf_size];
        UINT read_done = 0;

        while (read_done < bytes_to_read) {
            file.read(pBlockData, MIN(buf_size, bytes_to_read - read_done));
            int rd = file.gcount();
            out.write(pBlockData, rd);
            read_done += rd;
        }

        delete [] pBlockData;

        read_in_bytes += bytes_to_read;

        if (next_page_addr != V8_FF_SIGNATURE) { // есть следующая страница
            //pBlockHeader = (stBlockHeader*) &pFileData[next_page_addr];
            file.seekg(next_page_addr, std::ios_base::beg);
            file.read((char*)&Header, sizeof(Header));
        } else
            break;
    }

    if (BlockDataSize)
        *BlockDataSize = data_size;

    return 0;
}

bool CV8File::IsV8File(std::basic_ifstream<char> &file)
{
    stFileHeader FileHeader;
    stBlockHeader BlockHeader;

    stBlockHeader *pBlockHeader = &BlockHeader;

	memset(pBlockHeader, 0, sizeof(BlockHeader));

    std::ifstream::pos_type offset = file.tellg();

    file.read((char*)&FileHeader, sizeof(FileHeader));
    file.read((char*)&BlockHeader, sizeof(BlockHeader));

    file.seekg(offset);
    file.clear();

    if (pBlockHeader->EOL_0D != 0x0d ||
            pBlockHeader->EOL_0A != 0x0a ||
            pBlockHeader->space1 != 0x20 ||
            pBlockHeader->space2 != 0x20 ||
            pBlockHeader->space3 != 0x20 ||
            pBlockHeader->EOL2_0D != 0x0d ||
            pBlockHeader->EOL2_0A != 0x0a) {

        return false;
    }

    return true;
}

bool CV8File::IsV8File(const char *pFileData, ULONG FileDataSize)
{

    if (!pFileData) {
        return false;
    }

    // проверим чтобы длина файла не была меньше длины заголовка файла и заголовка блока адресов
    if (FileDataSize < stFileHeader::Size() + stBlockHeader::Size())
        return false;

    stFileHeader *pFileHeader = (stFileHeader*) pFileData;

    stBlockHeader *pBlockHeader;

    pBlockHeader = (stBlockHeader*) &pFileHeader[1];

    if (pBlockHeader->EOL_0D != 0x0d ||
            pBlockHeader->EOL_0A != 0x0a ||
            pBlockHeader->space1 != 0x20 ||
            pBlockHeader->space2 != 0x20 ||
            pBlockHeader->space3 != 0x20 ||
            pBlockHeader->EOL2_0D != 0x0d ||
            pBlockHeader->EOL2_0A != 0x0a) {

        return false;
    }

    return true;
}

int CV8File::PackFromFolder(const std::string &dirname, const std::string &filename_out)
{

    std::string filename;

    boost::filesystem::path p_curdir(dirname);

    filename = dirname;
    filename += "/FileHeader";

    boost::filesystem::path path_header(filename);
    boost::filesystem::ifstream file_in(path_header, std::ios_base::binary);

    file_in.seekg(0, std::ios_base::end);
    size_t filesize = file_in.tellg();
    file_in.seekg(0, std::ios_base::beg);

    file_in.read((char *)&FileHeader, filesize);
    file_in.close();

    boost::filesystem::directory_iterator d_end;
    boost::filesystem::directory_iterator it(p_curdir);

    Elems.clear();

    for (; it != d_end; it++) {
        boost::filesystem::path current_file(it->path());
        if (current_file.extension().string() == ".header") {

            filename = dirname;
            filename += current_file.filename().string();

            CV8Elem elem;

            {
                boost::filesystem::ifstream file_in(current_file, std::ios_base::binary);
                file_in.seekg(0, std::ios_base::end);

                elem.HeaderSize = file_in.tellg();
                elem.pHeader = new char[elem.HeaderSize];

                file_in.seekg(0, std::ios_base::beg);
                file_in.read((char *)elem.pHeader, elem.HeaderSize);
            }

            boost::filesystem::path data_path = current_file.replace_extension("data");
            {
                boost::filesystem::ifstream file_in(data_path, std::ios_base::binary);
                file_in.seekg(0, std::ios_base::end);
                elem.DataSize = file_in.tellg();
                file_in.seekg(0, std::ios_base::beg);
                elem.pData = new char[elem.DataSize];

                file_in.read((char *)elem.pData, elem.DataSize);
            }

            Elems.push_back(elem);

        }
    } // for it

    SaveFile(filename_out);

    return 0;
}


int CV8File::SaveBlockData(std::basic_ofstream<char> &file_out, const char *pBlockData, UINT BlockDataSize, UINT PageSize)
{

    if (PageSize < BlockDataSize)
        PageSize = BlockDataSize;

    stBlockHeader CurBlockHeader;

    CurBlockHeader.EOL_0D = 0xd;
    CurBlockHeader.EOL_0A = 0xa;
    CurBlockHeader.EOL2_0D = 0xd;
    CurBlockHeader.EOL2_0A = 0xa;

    CurBlockHeader.space1 = 0;
    CurBlockHeader.space2 = 0;
    CurBlockHeader.space3 = 0;

    char buf[20];

    sprintf(buf, "%08x", BlockDataSize);
    strncpy(CurBlockHeader.data_size_hex, buf, 8);

    sprintf(buf, "%08x", PageSize);
    strncpy(CurBlockHeader.page_size_hex, buf, 8);

    sprintf(buf, "%08x", V8_FF_SIGNATURE);
    strncpy(CurBlockHeader.next_page_addr_hex, buf, 8);

    CurBlockHeader.space1 = ' ';
    CurBlockHeader.space2 = ' ';
    CurBlockHeader.space3 = ' ';

    file_out.write(reinterpret_cast<char *>(&CurBlockHeader), sizeof(CurBlockHeader));

    file_out.write(reinterpret_cast<const char *>(pBlockData), BlockDataSize);

    char N = 0;
    for(UINT i = 0; i < PageSize - BlockDataSize; i++) {
        file_out.write(&N, 1);
    }

    return 0;
}

int CV8File::Parse(const std::string &filename_in, const std::string &dirname, int level)
{
    int ret = 0;

    boost::filesystem::ifstream file_in(filename_in, std::ios_base::binary);

    if (!file_in) {
        std::cerr << "UnpackToFolder. Input file not found!" << std::endl;
        return -1;
    }

    file_in.seekg(0, std::ios_base::end);
    ULONG FileDataSize = file_in.tellg();
    file_in.seekg(0, std::ios_base::beg);

    ret = UnpackToDirectoryNoLoad(dirname, file_in, FileDataSize);

    if (ret == V8UNPACK_NOT_V8_FILE) {
        std::cerr << "UnpackToFolder. This is not V8 file!" << std::endl;
        return ret;
    }

    std::cout << "LoadFile: ok" << std::endl;

    return ret;
}


int CV8File::SaveFileToFolder(const std::string &dirname) const
{

    int ret = 0;

    if (!boost::filesystem::exists(dirname)) {
        ret = !boost::filesystem::create_directory(dirname);
        if (ret && errno == ENOENT) {
            std::cerr << "SaveFileToFolder. Error in creating directory `" << dirname << "` !" << std::endl;
            return ret;
        }
    }
    ret = 0;

    std::string filename_out;

    char ElemName[512];
    UINT ElemNameLen;

    bool print_progress = true;
    UINT one_percent = Elems.size() / 50;
    if (print_progress && one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    UINT ElemNum = 0;
    std::vector<CV8Elem>::const_iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); elem++) {

        ++ElemNum;
        if (print_progress && ElemNum && one_percent && ElemNum%one_percent == 0) {
            if (ElemNum % (one_percent*10) == 0)
                std::cout << "|" << std::flush;
            else
                std::cout << ".";
        }

        GetElemName(*elem, ElemName, &ElemNameLen);

        filename_out = dirname;
        filename_out += "/";
        filename_out += ElemName;

        if (!elem->IsV8File) {
            boost::filesystem::ofstream file_out(filename_out, std::ios_base::binary);
            if (!file_out) {
                std::cerr << "SaveFile. Error in creating file!" << std::endl;
                return -1;
            }
            file_out.write(reinterpret_cast<char *>(elem->pData), elem->DataSize);
        } else {
            ret = elem->UnpackedData.SaveFileToFolder(filename_out);
            if (ret)
                break;
        }
    }

    if (print_progress && one_percent) {
        std::cout << std::endl << std::flush;
    }

    return ret;
}

int CV8File::GetElemName(const CV8Elem &Elem, char *ElemName, UINT *ElemNameLen) const
{
    *ElemNameLen = (Elem.HeaderSize - CV8Elem::stElemHeaderBegin::Size()) / 2;
    for (UINT j = 0; j < *ElemNameLen * 2; j+=2)
        ElemName[j/2] = Elem.pHeader[CV8Elem::stElemHeaderBegin::Size() + j];

    return 0;
}


int CV8File::SetElemName(CV8Elem &Elem, const char *ElemName, UINT ElemNameLen)
{
    UINT stElemHeaderBeginSize = CV8Elem::stElemHeaderBegin::Size();

    for (UINT j = 0; j <ElemNameLen * 2; j+=2, stElemHeaderBeginSize+=2) {
        Elem.pHeader[stElemHeaderBeginSize] = ElemName[j/2];
        Elem.pHeader[stElemHeaderBeginSize + 1] = 0;
    }

    return 0;
}



int CV8File::LoadFileFromFolder(const std::string &dirname)
{

    std::string filename;

    boost::filesystem::ifstream file_in;

    FileHeader.next_page_addr = V8_FF_SIGNATURE;
    FileHeader.page_size = V8_DEFAULT_PAGE_SIZE;
    FileHeader.storage_ver = 0;
    FileHeader.reserved = 0;

    Elems.clear();

    boost::filesystem::directory_iterator d_end;
    boost::filesystem::directory_iterator dit(dirname);

    for (; dit != d_end; ++dit) {
        boost::filesystem::path current_file(dit->path());
        if (current_file.filename().string().at(0) == '.')
            continue;

        CV8Elem elem;
        std::string name = current_file.filename().string();

        elem.HeaderSize = CV8Elem::stElemHeaderBegin::Size() + name.size() * 2 + 4; // последние четыре всегда нули?
        elem.pHeader = new char[elem.HeaderSize];

        memset(elem.pHeader, 0, elem.HeaderSize);

        SetElemName(elem, name.c_str(), name.size());

        if (boost::filesystem::is_directory(current_file)) {

            elem.IsV8File = true;

            std::string new_dirname(dirname);
            new_dirname += "/";
            new_dirname += name;

            elem.UnpackedData.LoadFileFromFolder(new_dirname);

        } else {
            elem.IsV8File = false;

            elem.DataSize = boost::filesystem::file_size(current_file);
            elem.pData = new char[elem.DataSize];

            boost::filesystem::ifstream file_in(current_file, std::ios_base::binary);
            file_in.read(reinterpret_cast<char *>(elem.pData), elem.DataSize);
        }

        Elems.push_back(elem);

    } // for directory_iterator

    return 0;

}

int CV8File::Build(const std::string &dirname, const std::string &filename, int level)
{
    int load = LoadFileFromFolder(dirname);
    if (load != 0)
        return load;

    std::cout << "LoadFileFromFolder: ok" << std::endl;

    Pack();

    std::cout << "Pack: ok" << std::endl;

    SaveFile(filename);

    return 0;
}

int CV8File::SaveFile(const std::string &filename)
{
    boost::filesystem::ofstream file_out(filename, std::ios_base::binary);
    if (!file_out) {
        std::cerr << "SaveFile. Error in creating file!" << std::endl;
        return -1;
    }

    // Создаем и заполняем данные по адресам элементов
    ElemsAddrs.clear();

    UINT ElemsNum = Elems.size();
    ElemsAddrs.reserve(ElemsNum);

    DWORD cur_block_addr = stFileHeader::Size() + stBlockHeader::Size();
    if (sizeof(stElemAddr) * ElemsNum < V8_DEFAULT_PAGE_SIZE)
        cur_block_addr += V8_DEFAULT_PAGE_SIZE;
    else
        cur_block_addr += stElemAddr::Size() * ElemsNum;

    std::vector<CV8Elem>::const_iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {

        stElemAddr addr;

        addr.elem_header_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader) + elem->HeaderSize;

        addr.elem_data_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader);

        if (elem->DataSize > V8_DEFAULT_PAGE_SIZE)
            cur_block_addr += elem->DataSize;
        else
            cur_block_addr += V8_DEFAULT_PAGE_SIZE;

        addr.fffffff = V8_FF_SIGNATURE;

        ElemsAddrs.push_back(addr);

    }


    // записываем заголовок
    file_out.write(reinterpret_cast<char*>(&FileHeader), sizeof(FileHeader));

    // записываем адреса элементов
    SaveBlockData(file_out, (char*) ElemsAddrs.data(), stElemAddr::Size() * ElemsNum);

    // записываем элементы (заголовок и данные)
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
        SaveBlockData(file_out, elem->pHeader, elem->HeaderSize, elem->HeaderSize);
        SaveBlockData(file_out, elem->pData, elem->DataSize);
    }

    return 0;

}

int CV8File::BuildCfFile(const std::string &in_dirname, const std::string &out_filename, bool dont_pack)
{
    //filename can't be empty
    if (!in_dirname.size()) {
        fputs("Argument error - Set of `in_dirname' argument \n", stderr);
        std::cerr << "Argument error - Set of `in_dirname' argument" << std::endl;
        return SHOW_USAGE;
    }

    if (!out_filename.size()) {
        std::cerr << "Argument error - Set of `out_filename' argument" << std::endl;
        return SHOW_USAGE;
    }

    if (!boost::filesystem::exists(in_dirname)) {
        std::cerr << "Source directory does not exist!" << std::endl;
        return -1;
    }

    UINT ElemsNum = 0;
    {
        boost::filesystem::directory_iterator d_end;
        boost::filesystem::directory_iterator dit(in_dirname);

        for (; dit != d_end; ++dit) {

            boost::filesystem::path current_file(dit->path());
            std::string name = current_file.filename().string();

            if (name.at(0) == '.')
                continue;

            ++ElemsNum;
        }
    }


    //Предварительные расчеты длины заголовка таблицы содержимого TOC файла
    FileHeader.next_page_addr = V8_FF_SIGNATURE;
    FileHeader.page_size = V8_DEFAULT_PAGE_SIZE;
    FileHeader.storage_ver = 0;
    FileHeader.reserved = 0;
    DWORD cur_block_addr = stFileHeader::Size() + stBlockHeader::Size();
    stElemAddr *pTOC;
    pTOC = new stElemAddr[ElemsNum];
    if (sizeof(stElemAddr) * ElemsNum < V8_DEFAULT_PAGE_SIZE)
        cur_block_addr += V8_DEFAULT_PAGE_SIZE;
    else
        cur_block_addr += stElemAddr::Size() * ElemsNum;

    boost::filesystem::ofstream file_out(out_filename, std::ios_base::binary);
    //Открываем выходной файл контейнер на запись
    if (!file_out) {
        std::cout << "SaveFile. Error in creating file!" << std::endl;
        return -1;
    }

    //Резервируем место в начале файла под заголовок и TOC
    for(unsigned i=0; i < cur_block_addr; i++) {
        file_out << '\0';
    }

    UINT one_percent = ElemsNum / 50;
    if (one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    std::string new_dirname;

    UINT ElemNum = 0;

    boost::filesystem::directory_iterator d_end;
    boost::filesystem::directory_iterator dit(in_dirname);
    for (; dit != d_end; ++dit) {

        boost::filesystem::path current_file(dit->path());
        std::string name = current_file.filename().string();

        if (name.at(0) == '.')
            continue;


        //Progress bar ->
        {
            if (ElemNum && one_percent && ElemNum%one_percent == 0) {
                if (ElemNum % (one_percent*10) == 0)
                    std::cout << "|" << std::flush;
                else
                    std::cout << ".";
            }
        }//<- Progress bar

        CV8Elem pElem;

        pElem.HeaderSize = CV8Elem::stElemHeaderBegin::Size() + name.size() * 2 + 4; // последние четыре всегда нули?
        pElem.pHeader = new char[pElem.HeaderSize];

        memset(pElem.pHeader, 0, pElem.HeaderSize);

        SetElemName(pElem, name.c_str(), name.size());
        if (boost::filesystem::is_directory(current_file)) {

            pElem.IsV8File = true;

            std::string new_dirname(in_dirname);
            new_dirname += "/";
            new_dirname += name;

            pElem.UnpackedData.LoadFileFromFolder(new_dirname);

        } else {

            pElem.IsV8File = false;

            pElem.DataSize = boost::filesystem::file_size(current_file);
            pElem.pData = new char[pElem.DataSize];

            boost::filesystem::path p_filename(in_dirname);
            p_filename /= name;

            boost::filesystem::ifstream file_in(p_filename, std::ios_base::binary);
            file_in.read(reinterpret_cast<char*>(pElem.pData), pElem.DataSize);
        }

        if (!dont_pack) {
            //Сжимаем данные
            PackElem(pElem);
        }

        //Добавляем элемент в TOC
        pTOC[ElemNum].elem_header_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader) + pElem.HeaderSize;
        pTOC[ElemNum].elem_data_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader);
        if (pElem.DataSize > V8_DEFAULT_PAGE_SIZE)
            cur_block_addr += pElem.DataSize;
        else
            cur_block_addr += V8_DEFAULT_PAGE_SIZE;
        pTOC[ElemNum].fffffff = V8_FF_SIGNATURE;
        //Записываем элемент в файл
        SaveBlockData(file_out, pElem.pHeader, pElem.HeaderSize, pElem.HeaderSize);
        SaveBlockData(file_out, pElem.pData, pElem.DataSize);

        //Освобождаем память
        delete[] pElem.pData;
        pElem.pData = NULL;
        delete[] pElem.pHeader;
        pElem.pHeader = NULL;
        pElem.IsV8File = false;
        pElem.HeaderSize = 0;
        pElem.DataSize = 0;

        ElemNum++;
    }

    //Записывем заголовок файла
    file_out.seekp(0, std::ios_base::beg);
    file_out.write(reinterpret_cast<const char*>(&FileHeader), sizeof(FileHeader));

    //Записываем блок TOC
    SaveBlockData(file_out, (const char*) pTOC, stElemAddr::Size() * ElemsNum);

    delete [] pTOC;

    std::cout << std::endl << "Build OK!";

    return 0;
}

int CV8File::PackElem(CV8Elem &pElem)
{
    char *DeflateBuffer = NULL;
    ULONG DeflateSize = 0;

    char *DataBuffer = NULL;
    ULONG DataBufferSize = 0;

    int ret = 0;
    if (!pElem.IsV8File) {
        ret = Deflate(pElem.pData, &DeflateBuffer, pElem.DataSize, &DeflateSize);
        if (ret)
            return ret;

        delete[] pElem.pData;
        pElem.pData = new char[DeflateSize];
        pElem.DataSize = DeflateSize;
        memcpy(pElem.pData, DeflateBuffer, DeflateSize);
    } else {
        pElem.UnpackedData.GetData(&DataBuffer, &DataBufferSize);

        ret = Deflate(DataBuffer, &DeflateBuffer, DataBufferSize, &DeflateSize);
        if (ret)
            return ret;

        //pElem.UnpackedData = CV8File();
        pElem.IsV8File = false;

        pElem.pData = new char[DeflateSize];
        pElem.DataSize = DeflateSize;
        memcpy(pElem.pData, DeflateBuffer, DeflateSize);

    }

    if (DeflateBuffer)
        free(DeflateBuffer);

    if (DataBuffer)
        free(DataBuffer);

    return 0;
}

int CV8File::Pack()
{
    char *DeflateBuffer = NULL;
    ULONG DeflateSize = 0;

    char *DataBuffer = NULL;
    ULONG DataBufferSize = 0;

    int ret = 0;

    bool print_progress = true;
    UINT ElemsNum = Elems.size();
    UINT one_percent = ElemsNum / 50;
    if (print_progress && one_percent) {
        std::cout << "Progress (50 points): " << std::flush;
    }


    UINT ElemNum = 0;

    std::vector<CV8Elem>::/*const_*/iterator elem;
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {

        ++ElemNum;
        if (print_progress && ElemNum && one_percent && ElemNum%one_percent == 0) {
            if (ElemNum % (one_percent*10) == 0)
                std::cout << "|" << std::flush;
            else
                std::cout << ".";
        }

        if (!elem->IsV8File) {
            ret = Deflate(elem->pData, &DeflateBuffer, elem->DataSize, &DeflateSize);
            if (ret)
                return ret;

            delete[] elem->pData;
            elem->pData = new char[DeflateSize];
            elem->DataSize = DeflateSize;
            memcpy(elem->pData, DeflateBuffer, DeflateSize);
        } else {
            elem->UnpackedData.GetData(&DataBuffer, &DataBufferSize);

            ret = Deflate(DataBuffer, &DeflateBuffer, DataBufferSize, &DeflateSize);
            if (ret)
                return ret;

            elem->IsV8File = false;

            elem->pData = new char[DeflateSize];
            elem->DataSize = DeflateSize;
            memcpy(elem->pData, DeflateBuffer, DeflateSize);

        }


    }

    if (print_progress && one_percent) {
        std::cout << std::endl;
    }

    if (DeflateBuffer)
        free(DeflateBuffer);

    if (DataBuffer)
        free(DataBuffer);

    return 0;
}


int CV8File::GetData(char **DataBuffer, ULONG *DataBufferSize) const
{

    UINT ElemsNum = Elems.size();

    ULONG NeedDataBufferSize = 0;
    NeedDataBufferSize += stFileHeader::Size();

    // заголовок блока и данные блока - адреса элементов с учетом минимальной страницы 512 байт
    NeedDataBufferSize += stBlockHeader::Size() + MAX(stElemAddr::Size() * ElemsNum, V8_DEFAULT_PAGE_SIZE);

    std::vector<CV8Elem>::const_iterator elem;
    //for(ElemNum = 0; ElemNum < ElemsNum; ElemNum++)
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {

        // заголовок блока и данные блока - заголовок элемента
        NeedDataBufferSize += stBlockHeader::Size()  + elem->HeaderSize;

        // заголовок блока и данные блока - данные элемента с учетом минимальной страницы 512 байт
        NeedDataBufferSize += stBlockHeader::Size()  + MAX(elem->DataSize, V8_DEFAULT_PAGE_SIZE);
    }


    // Создаем и заполняем данные по адресам элементов
    stElemAddr *pTempElemsAddrs = new stElemAddr[ElemsNum], *pCurrentTempElem;
    pCurrentTempElem = pTempElemsAddrs;

    DWORD cur_block_addr = stFileHeader::Size() + stBlockHeader::Size();
    if (stElemAddr::Size() * ElemsNum < V8_DEFAULT_PAGE_SIZE)
        cur_block_addr += V8_DEFAULT_PAGE_SIZE;
    else
        cur_block_addr += stElemAddr::Size() * ElemsNum;

    for (elem = Elems.begin(); elem !=  Elems.end(); ++elem) {

        pCurrentTempElem->elem_header_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader) + elem->HeaderSize;

        pCurrentTempElem->elem_data_addr = cur_block_addr;
        cur_block_addr += sizeof(stBlockHeader);

        if (elem->DataSize > V8_DEFAULT_PAGE_SIZE)
            cur_block_addr += elem->DataSize;
        else
            cur_block_addr += V8_DEFAULT_PAGE_SIZE;

        pCurrentTempElem->fffffff = V8_FF_SIGNATURE;
        ++pCurrentTempElem;
    }


    *DataBuffer = static_cast<char*> (realloc(*DataBuffer, NeedDataBufferSize));


    char *cur_pos = *DataBuffer;


    // записываем заголовок
    memcpy(cur_pos, (char*) &FileHeader, stFileHeader::Size());
    cur_pos += stFileHeader::Size();

    // записываем адреса элементов
    SaveBlockDataToBuffer(&cur_pos, (char*) pTempElemsAddrs, stElemAddr::Size() * ElemsNum);

    // записываем элементы (заголовок и данные)
    for (elem = Elems.begin(); elem != Elems.end(); ++elem) {
        SaveBlockDataToBuffer(&cur_pos, elem->pHeader, elem->HeaderSize, elem->HeaderSize);
        SaveBlockDataToBuffer(&cur_pos, elem->pData, elem->DataSize);
    }

    //fclose(file_out);

    if (pTempElemsAddrs)
        delete[] pTempElemsAddrs;

    *DataBufferSize = NeedDataBufferSize;

    return 0;

}


int CV8File::SaveBlockDataToBuffer(char **cur_pos, const char *pBlockData, UINT BlockDataSize, UINT PageSize) const
{

    if (PageSize < BlockDataSize)
        PageSize = BlockDataSize;

    stBlockHeader CurBlockHeader;

    CurBlockHeader.EOL_0D = 0xd;
    CurBlockHeader.EOL_0A = 0xa;
    CurBlockHeader.EOL2_0D = 0xd;
    CurBlockHeader.EOL2_0A = 0xa;

    CurBlockHeader.space1 = 0;
    CurBlockHeader.space2 = 0;
    CurBlockHeader.space3 = 0;

    char buf[20];

    sprintf(buf, "%08x", BlockDataSize);
    strncpy(CurBlockHeader.data_size_hex, buf, 8);

    sprintf(buf, "%08x", PageSize);
    strncpy(CurBlockHeader.page_size_hex, buf, 8);

    sprintf(buf, "%08x", V8_FF_SIGNATURE);
    strncpy(CurBlockHeader.next_page_addr_hex, buf, 8);

    CurBlockHeader.space1 = ' ';
    CurBlockHeader.space2 = ' ';
    CurBlockHeader.space3 = ' ';


    memcpy(*cur_pos, (char*)&CurBlockHeader, stBlockHeader::Size());
    *cur_pos += stBlockHeader::Size();


    memcpy(*cur_pos, pBlockData, BlockDataSize);
    *cur_pos += BlockDataSize;

    for(UINT i = 0; i < PageSize - BlockDataSize; i++) {
        **cur_pos = 0;
        ++*cur_pos;
    }

    return 0;
}
