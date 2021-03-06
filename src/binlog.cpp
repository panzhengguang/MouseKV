//
// Created by zgpan on 2019/4/14.
//

#include "binlog.h"
#include "log.h"
#include <sys/mman.h>

BinLog::BinLog()
{
    m_fileName = "";
    m_fp = nullptr;
    m_writtenSize = 0;
}

BinLog::~BinLog()
{
    close();
}

bool BinLog::open(const std::string &file_name)
{
    close(); //之前的老的文件close -- 然后此处再close会core dump -- double free or corruption

    //打开文件
    FILE* fp = fopen(file_name.c_str(),"a+");
    if(!fp)
    {
        COMM_LOG(Logger::ERROR,"open binlog failed");
        return false;
    }

    //将文件指针指向末尾
    if(fseek(fp,0,SEEK_END) != 0)
    {
        fclose(fp);
        COMM_LOG(Logger::ERROR,"seek binlog end failed");
        return false;
    }

    int file_size = ftell(fp);
    //指向文件的开头
    if(fseek(fp,0,SEEK_SET))
    {
        fclose(fp);
        COMM_LOG(Logger::ERROR,"seek binlog begin failed");
        return false;
    }

    //判断的大小
    if(file_size < sizeof(Header))
    {
        Header header;
        //fwrite的返回值是写入元素的个数
        auto ret_size = fwrite(&header, sizeof(Header),1,fp);
        if(ret_size != 1)
        {
            fclose(fp);
            COMM_LOG(Logger::ERROR,"fwrite ret error, ret_size[%zu]", ret_size);
            return false;
        }
    }
    else
    {
        Header header;
        auto ret_size = fread(&header, sizeof(Header),1,fp);
        if(ret_size != 1 || !header.IsHeaderValid())
        {
            fclose(fp);
            COMM_LOG(Logger::ERROR,"check head error, ret_size[%zu]", ret_size);
            return false;
        }
    }

    //如果head无问题，则将指针指向末尾
    if(fseek(fp,0,SEEK_END) != 0)
    {
        COMM_LOG(Logger::ERROR,"seek to end failed");
        return false;
    }

    //成员变量赋值
    m_writtenSize = ftell(fp);
    m_fp = fp;
    m_fileName = file_name;
    return true;
}

//需要及时把缓存区数据刷新到内核中
void BinLog::sync()
{
    if(m_fp)
    {
        fflush(m_fp);
    }
}

void BinLog::close()
{
    COMM_LOG(Logger::DEBUG,"close file");
    if(m_fp)
    {
        fclose(m_fp);
        m_fp = nullptr;
    }
}

//删除一条日志
bool BinLog::appendDelRecord(const string &key)
{
    int writesize = sizeof(LogItem) + key.size();
    LogItem item;
    item.item_size = writesize;
    item.key_size = key.size();
    item.value_size = 0;
    item.type = LogItem::DEL;

    size_t  succsize = fwrite(&item, sizeof(item), 1 ,m_fp);
    succsize = fwrite(key.data(), key.size(), 1, m_fp);

    m_writtenSize += writesize;
    sync();
    return true;
}

//插入一条日志
bool BinLog::appendSetRecord(const string &key, const string &value)
{
    COMM_LOG(Logger::DEBUG, "appendSetRecord begin, key[%s] value[%s] m_writtenSize[%d]", key.c_str(), value.c_str(), m_writtenSize);
    int writesize = sizeof(LogItem) + key.size() + value.size();
    LogItem item;
    item.item_size = writesize;
    item.key_size = key.size();
    item.value_size = value.size();
    item.type = LogItem::SET;

    size_t  succsize = fwrite(&item, sizeof(item), 1 ,m_fp);
    COMM_LOG(Logger::DEBUG,"write item, ret[%d]", succsize);
    succsize = fwrite(key.data(), key.size(), 1, m_fp);
    COMM_LOG(Logger::DEBUG,"write item, ret[%d]", succsize);
    succsize = fwrite(value.data(), value.size(), 1, m_fp);
    COMM_LOG(Logger::DEBUG,"write item, ret[%d]", succsize);
    m_writtenSize += writesize;
    COMM_LOG(Logger::DEBUG,"write item, m_writtenSize[%d]", m_writtenSize);
    sync();
    return true;
}

int BinlogFileList::indexOfFileName(const string &fileName) const
{
    for ( size_t i = 0; i < m_binlogFileList.size(); ++i) {
        if(m_binlogFileList[i] == fileName)
            return i;
    }
    return -1;
}

void BinlogFileList::loadBinlogListFromIndex()
{
    m_binlogFileList.clear();
    TextConfigFile::read(m_default_index_file, m_binlogFileList);
}

void BinlogFileList::saveBinlogListToIndex()
{
    TextConfigFile::write(m_default_index_file,m_binlogFileList);
}


BinlogParser::BinlogParser()
{
    m_base = nullptr;
    m_size = 0;
    m_fd = -1;
}

BinlogParser::~BinlogParser()
{
    close();
}


bool BinlogParser::open(const std::string &fname)
{
    close();
    //先获取下文件大小
    struct stat sbuf;
    int size;
    if(::stat(fname.c_str(), &sbuf) != 0)
    {
        COMM_LOG(Logger::ERROR, "BinlogParser::parse: stat(%s): %s", fname.c_str(), strerror(errno));
        return false;
    }
    else
    {
        size = sbuf.st_size;
    }
    int fd = ::open(fname.c_str(), O_RDONLY);
    if (fd < 0)
    {
        COMM_LOG(Logger::ERROR, "BinlogParser::parse: open: %s", strerror(errno));
        return false;
    }

    BinLog::Header header;
    if (::read(fd, (char*)&header, sizeof(header)) != sizeof(header))
    {
        ::close(fd);
        COMM_LOG(Logger::ERROR, "BinlogParser::parse: read: %s", strerror(errno));
        return false;
    }

    if (!header.IsHeaderValid())
    {
        ::close(fd);
        COMM_LOG(Logger::ERROR, "BinlogParser::parse: file invalid");
        return false;
    }

    void* base = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
    {
        ::close(fd);
        COMM_LOG(Logger::ERROR, "BinlogParser::parse: mmap: %s", strerror(errno));
        return false;
    }
    m_base = (char*)base;
    m_size = size;
    m_fd = fd;
    return true;
}

void BinlogParser::close()
{
    if(m_base != nullptr)
    {
        munmap(m_base, m_size);
        m_base = nullptr;
        m_size = 0;
    }

    if ( m_fd != -1)
    {
        ::close(m_fd);
        m_fd = -1;
    }
}

BinlogBufferReader BinlogParser::reader() const
{
    char* buff = m_base + sizeof(BinLog::Header);
    int size = m_size - sizeof(BinLog::Header);
    return BinlogBufferReader(buff, size);
}