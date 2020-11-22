/*
 * =====================================================================================
 *
 *       Filename: zsdb.cpp
 *        Created: 11/13/2018 22:58:28
 *    Description: 
 *
 *        Version: 1.0
 *       Revision: none
 *       Compiler: gcc
 *
 *         Author: ANHONG
 *          Email: anhonghe@gmail.com
 *   Organization: USTC
 *
 * =====================================================================================
 */

#include <regex>
#include <zstd.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cinttypes>
#include <algorithm>
#include <filesystem>

#include "zsdb.hpp"
#include "fileptr.hpp"
#include "fflerror.hpp"
#include "totype.hpp"

constexpr static int g_compLevel = 3;

static std::vector<uint8_t> compressDataBuf(const uint8_t *pDataBuf, size_t nDataLen, ZSTD_CCtx *pCCtx, const ZSTD_CDict *pCDict)
{
    if(!pDataBuf || !nDataLen){
        return {};
    }

    size_t nRC = 0;
    std::vector<uint8_t> stRetBuf(ZSTD_compressBound(nDataLen), 0);

    if(pCCtx && pCDict){
        nRC = ZSTD_compress_usingCDict(pCCtx, stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen, pCDict);
    }else if(pCCtx){
        nRC = ZSTD_compressCCtx(pCCtx, stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen, g_compLevel);
    }else{
        nRC = ZSTD_compress(stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen, g_compLevel);
    }

    if(ZSTD_isError(nRC)){
        return {};
    }

    stRetBuf.resize(nRC);
    return stRetBuf;
}

static std::vector<uint8_t> decompressDataBuf(const uint8_t *pDataBuf, size_t nDataLen, ZSTD_DCtx *pDCtx, const ZSTD_DDict *pDDict)
{
    if(!pDataBuf || !nDataLen){
        return {};
    }

    std::vector<uint8_t> stRetBuf;
    switch(auto nDecompSize = ZSTD_getFrameContentSize(pDataBuf, nDataLen)){
        case ZSTD_CONTENTSIZE_ERROR:
        case ZSTD_CONTENTSIZE_UNKNOWN:
            {
                return {};
            }
        default:
            {
                stRetBuf.resize(nDecompSize);
                break;
            }
    }

    size_t nRC = 0;
    if(pDCtx && pDDict){
        nRC = ZSTD_decompress_usingDDict(pDCtx, stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen, pDDict);
    }else if(pDCtx){
        nRC = ZSTD_decompressDCtx(pDCtx, stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen);
    }else{
        nRC = ZSTD_decompress(stRetBuf.data(), stRetBuf.size(), pDataBuf, nDataLen);
    }

    if(ZSTD_isError(nRC)){
        return {};
    }

    stRetBuf.resize(nRC);
    return stRetBuf;
}

static std::vector<uint8_t> readFileData(const char *szPath)
{
    if(!szPath){
        return {};
    }

    auto fp = make_fileptr(reinterpret_cast<const char *>(szPath), "rb");
    if(!fp){
        return {};
    }

    std::fseek(fp.get(), 0, SEEK_END);
    auto nFileLen = std::ftell(fp.get());
    std::fseek(fp.get(), 0, SEEK_SET);

    std::vector<uint8_t> stDataBuf(nFileLen, 0);
    if(std::fread(stDataBuf.data(), nFileLen, 1, fp.get()) != 1){
        throw fflerror("failed to read file: %s", szPath);
    }
    return stDataBuf;
}

static std::vector<uint8_t> readFileOffData(std::FILE *fp, size_t nDataOff, size_t nDataLen)
{
    if(!fp){
        return {};
    }

    std::vector<uint8_t> stReadBuf(nDataLen);
    if(std::fseek(fp, nDataOff, SEEK_SET)){
        return {};
    }

    if(std::fread(stReadBuf.data(), stReadBuf.size(), 1, fp) != 1){
        return {};
    }

    return stReadBuf;
}

static std::vector<uint8_t> decompFileOffData(std::FILE *fp, size_t nDataOff, size_t nDataLen, ZSTD_DCtx *pDCtx, const ZSTD_DDict *pDDict)
{
    auto stCompBuf = readFileOffData(fp, nDataOff, nDataLen);
    if(stCompBuf.empty()){
        return {};
    }

    return decompressDataBuf(stCompBuf.data(), stCompBuf.size(), pDCtx, pDDict);
}

const ZSDB::InnEntry &ZSDB::GetErrorEntry()
{
    const static auto s_ErrorEntry = []() -> InnEntry
    {
        InnEntry stErrorEntry;
        std::memset(&stErrorEntry, 0, sizeof(stErrorEntry));

        stErrorEntry.Offset    = 0X0123456789ABCDEF;
        stErrorEntry.Length    = 0XFEDCBA9876543210;
        stErrorEntry.FileName  = 0X0123456789ABCDEF;
        stErrorEntry.Attribute = 0XFEDCBA9876543210;

        return stErrorEntry;
    }();
    return s_ErrorEntry;
}

ZSDB::ZSDB(const char *szPath)
    : m_fp(nullptr)
    , m_DCtx(nullptr)
    , m_DDict(nullptr)
    , m_header()
    , m_entryList()
    , m_fileNameBuf()
{
    m_fp = std::fopen(szPath, "rb");
    if(!m_fp){
        throw fflerror("failed to open database file");
    }

    m_DCtx = ZSTD_createDCtx();
    if(!m_DCtx){
        throw fflerror("failed to create decompress context");
    }

    if(auto stHeaderData = readFileOffData(m_fp, 0, sizeof(ZSDBHeader)); stHeaderData.empty()){
        throw fflerror("failed to load izdb header");
    }else{
        std::memcpy(&m_header, stHeaderData.data(), sizeof(m_header));
    }

    if(m_header.DictLength){
        auto nOffset = check_cast<size_t>(m_header.DictOffset);
        auto nLength = check_cast<size_t>(m_header.DictLength);
        if(auto stDictBuf = decompFileOffData(m_fp, nOffset, nLength, m_DCtx, m_DDict); stDictBuf.empty()){
            throw fflerror("failed to load data at (off = %zu, length = %zu)", nOffset, nLength);
        }else{
            m_DDict = ZSTD_createDDict(stDictBuf.data(), stDictBuf.size());
            if(!m_DDict){
                throw fflerror("create decompression dictory failed");
            }
        }
    }

    if(m_header.EntryLength){
        auto nOffset = check_cast<size_t>(m_header.EntryOffset);
        auto nLength = check_cast<size_t>(m_header.EntryLength);

        if(auto stEntryBuf = decompFileOffData(m_fp, nOffset, nLength, m_DCtx, m_DDict); stEntryBuf.empty()){
            throw fflerror("failed to load data at (off = %zu, length = %zu)", nOffset, nLength);
        }else{
            if(stEntryBuf.size() != (1 + m_header.EntryNum) * sizeof(InnEntry)){
                throw fflerror("zsdb database file corrupted");
            }

            auto *pHead = (InnEntry *)(stEntryBuf.data());
            m_entryList.clear();
            m_entryList.insert(m_entryList.end(), pHead, pHead + m_header.EntryNum + 1);

            if(std::memcmp(&m_entryList.back(), &GetErrorEntry(), sizeof(InnEntry))){
                throw fflerror("zsdb database file corrupted");
            }
            m_entryList.pop_back();
        }
    }

    if(m_header.FileNameLength){
        auto nOffset = check_cast<size_t>(m_header.FileNameOffset);
        auto nLength = check_cast<size_t>(m_header.FileNameLength);
        if(auto stFileNameBuf = decompFileOffData(m_fp, nOffset, nLength, m_DCtx, nullptr); stFileNameBuf.empty()){
            throw fflerror("failed to load data at (off = %zu, length = %zu)", nOffset, nLength);
        }else{
            auto *pHead = (char *)(stFileNameBuf.data());
            m_fileNameBuf.clear();
            m_fileNameBuf.insert(m_fileNameBuf.end(), pHead, pHead + stFileNameBuf.size());
        }
    }
}

ZSDB::~ZSDB()
{
    std::fclose(m_fp);
    ZSTD_freeDCtx(m_DCtx);
    ZSTD_freeDDict(m_DDict);
}

const char *ZSDB::Decomp(const char *szFileName, size_t nCheckLen, std::vector<uint8_t> *pDstBuf)
{
    if(!szFileName || !std::strlen(szFileName)){
        return nullptr;
    }

    auto p = std::lower_bound(m_entryList.begin(), m_entryList.end(), szFileName, [this, nCheckLen](const InnEntry &lhs, const char *rhs) -> bool
    {
        if(nCheckLen){
            return std::strncmp(m_fileNameBuf.data() + lhs.FileName, rhs, nCheckLen) < 0;
        }else{
            return std::strcmp(m_fileNameBuf.data() + lhs.FileName, rhs) < 0;
        }
    });

    if(p == m_entryList.end()){
        return nullptr;
    }

    if(nCheckLen){
        if(std::strncmp(szFileName, m_fileNameBuf.data() + p->FileName, nCheckLen)){
            return nullptr;
        }
    }else{
        if(std::strcmp(szFileName, m_fileNameBuf.data() + p->FileName)){
            return nullptr;
        }
    }

    return ZSDB::DecompEntry(*p, pDstBuf) ? (m_fileNameBuf.data() + p->FileName) : nullptr;
}

bool ZSDB::DecompEntry(const ZSDB::InnEntry &rstEntry, std::vector<uint8_t> *pDstBuf)
{
    if(!pDstBuf){
        return false;
    }

    if(!rstEntry.Length){
        pDstBuf->clear();
        return true;
    }

    std::vector<uint8_t> stRetBuf;
    if(rstEntry.Attribute & F_COMPRESSED){
        stRetBuf = decompFileOffData(m_fp, m_header.StreamOffset + rstEntry.Offset, rstEntry.Length, m_DCtx, m_DDict);
    }else{
        stRetBuf = readFileOffData(m_fp, m_header.StreamOffset + rstEntry.Offset, rstEntry.Length);
    }

    if(stRetBuf.empty()){
        return false;
    }

    pDstBuf->swap(stRetBuf);
    return true;
}

std::vector<ZSDB::Entry> ZSDB::GetEntryList() const
{
    std::vector<ZSDB::Entry> stRetBuf;
    for(auto &rstEntry: m_entryList){
        stRetBuf.emplace_back(m_fileNameBuf.data() + rstEntry.FileName, rstEntry.Length, rstEntry.Attribute);
    }
    return stRetBuf;
}

bool ZSDB::BuildDB(const char *szSaveFullName, const char *szFileNameRegex, const char *szDataPath, const char *szDictPath, double fCompRatio)
{
    if(!szSaveFullName){
        return false;
    }

    if(!szDataPath){
        return false;
    }

    ZSTD_CDict *pCDict = nullptr;
    std::vector<uint8_t> stCDictBuf;

    if(szDictPath){
        stCDictBuf = readFileData(szDictPath);
        if(stCDictBuf.empty()){
            return false;
        }

        pCDict = ZSTD_createCDict(stCDictBuf.data(), stCDictBuf.size(), g_compLevel);
        if(!pCDict){
            return false;
        }
    }

    ZSTD_CCtx *pCCtx = ZSTD_createCCtx();
    if(!pCCtx){
        return false;
    }

    std::vector<char> stFileNameBuf;
    std::vector<uint8_t> stStreamBuf;
    std::vector<InnEntry> stEntryList;

    size_t nCount = 0;
    std::regex stFileNameReg(szFileNameRegex ? szFileNameRegex : ".*");

    for(auto &p: std::filesystem::directory_iterator(szDataPath)){
        if(!p.is_regular_file()){
            continue;
        }

        auto szFileName = p.path().filename().u8string();
        if(szFileNameRegex){
            if(!std::regex_match(reinterpret_cast<const char *>(szFileName.c_str()), stFileNameReg)){
                continue;
            }
        }

        auto stSrcBuf = readFileData(reinterpret_cast<const char *>(p.path().u8string().c_str()));
        if(stSrcBuf.empty()){
            continue;
        }

        auto stDstBuf = compressDataBuf(stSrcBuf.data(), stSrcBuf.size(), pCCtx, pCDict);
        if(stDstBuf.empty()){
            continue;
        }

        bool bCompressed = ((1.00 * stDstBuf.size() / stSrcBuf.size()) < fCompRatio);
        auto &rstCurrBuf = bCompressed ? stDstBuf : stSrcBuf;

        InnEntry stEntry;
        std::memset(&stEntry, 0, sizeof(stEntry));

        stEntry.Offset = stStreamBuf.size();
        stEntry.Length = rstCurrBuf.size();
        stStreamBuf.insert(stStreamBuf.end(), rstCurrBuf.begin(), rstCurrBuf.end());

        stEntry.FileName = stFileNameBuf.size();
        stFileNameBuf.insert(stFileNameBuf.end(), szFileName.begin(), szFileName.end());
        stFileNameBuf.push_back('\0');

        if(bCompressed){
            stEntry.Attribute |= F_COMPRESSED;
        }

        stEntryList.push_back(stEntry);
        nCount++;
    }

    std::sort(stEntryList.begin(), stEntryList.end(), [&stFileNameBuf](const InnEntry &lhs, const InnEntry &rhs) -> bool
    {
        return std::strcmp(stFileNameBuf.data() + lhs.FileName, stFileNameBuf.data() + rhs.FileName) < 0;
    });

    stEntryList.push_back(GetErrorEntry());

    ZSDBHeader stHeader;
    std::memset(&stHeader, 0, sizeof(stHeader));

    stHeader.ZStdVersion = ZSTD_versionNumber();
    stHeader.EntryNum    = nCount;

    stHeader.DictOffset = sizeof(stHeader);
    stHeader.DictLength = stCDictBuf.size();

    auto stEntryCompBuf = compressDataBuf((uint8_t *)(stEntryList.data()), stEntryList.size() * sizeof(InnEntry), pCCtx, nullptr);
    stHeader.EntryOffset = stHeader.DictOffset + stHeader.DictLength;
    stHeader.EntryLength = stEntryCompBuf.size();

    auto stFileNameCompBuf = compressDataBuf((uint8_t *)(stFileNameBuf.data()), stFileNameBuf.size(), pCCtx, nullptr);
    stHeader.FileNameOffset = stHeader.EntryOffset + stHeader.EntryLength;
    stHeader.FileNameLength = stFileNameCompBuf.size();

    stHeader.StreamOffset = stHeader.FileNameOffset + stHeader.FileNameLength;
    stHeader.StreamLength = stStreamBuf.size();

    auto fp = make_fileptr(szSaveFullName, "wb");
    if(!fp){
        return false;
    }

    if(std::fwrite(&stHeader, sizeof(stHeader), 1, fp.get()) != 1){
        return false;
    }

    if(std::fwrite(stEntryCompBuf.data(), stEntryCompBuf.size(), 1, fp.get()) != 1){
        return false;
    }

    if(std::fwrite(stFileNameCompBuf.data(), stFileNameCompBuf.size(), 1, fp.get()) != 1){
        return false;
    }

    if(std::fwrite(stStreamBuf.data(), stStreamBuf.size(), 1, fp.get()) != 1){
        return false;
    }

    return true;
}
