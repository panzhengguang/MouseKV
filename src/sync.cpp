//
// Created by zgpan on 2019/6/23.
//

#include "sync.h"
#include "binlog.h"
#include "log.h"


SyncThread::SyncThread(const char *master, int port, Mouse* svr)
{
    m_master_addr  = HostAddress(master, port);
    m_net_buf = new char[32*1024*1024];
    server = svr;
}


void SyncThread::terminate()
{
    m_is_running = false;
}

void* SyncThread::run(void *arg)
{
    SyncThread* thread = (SyncThread*)arg;
    //0. 读取索引文件记录的位置信息 -- 如果没有读取，则初始化为默认值
    bool ret = TextConfigFile::read(thread->m_binlog_index_file, thread->m_master_sync_info);
    if(ret){
        if(thread->m_master_sync_info.size() != 2){
            COMM_LOG(Logger::ERROR, "m_master_sync_info error, size[%zu]", thread->m_master_sync_info.size());
            return NULL;
        }
    } else{
        thread->m_master_sync_info.clear();
        thread->m_master_sync_info.emplace_back(" ");
        thread->m_master_sync_info.emplace_back("-1");
    }

    //1.创建tcp链接
    thread->m_master_sock = Socket::CreateSocket();
    if (!thread->m_master_sock.connect(thread->m_master_addr))
    {
        COMM_LOG(Logger::ERROR,"connect failed, master_ip[%s] master_port[%d]",
                thread->m_master_addr.ip(), thread->m_master_addr.port());
        return NULL;
    }

    //2. while循环--发送SYNC命令、接受同步数据、处理同步的数据
    while (thread->m_is_running)
    {
        //组装包
        int send_len = sprintf(thread->m_net_buf, "*3\r\n$6\r\n__SYNC\r\n$%d\r\n%s\r\n$%d\r\n%s\r\n",
                               (int)thread->m_master_sync_info[0].length(), thread->m_master_sync_info[0].c_str(),
                               (int)thread->m_master_sync_info[1].length(), thread->m_master_sync_info[1].c_str());
        int send_succ_len = thread->m_master_sock.send(thread->m_net_buf, send_len);
        if (send_succ_len < 0)
        {
            //发送失败了
            COMM_LOG(Logger::ERROR, "send failed, _send_len[%d] _send_succ_len[%d]", send_len, send_succ_len);
            continue;
        }
        COMM_LOG(Logger::DEBUG, "send succ, _send_len[%d] _send_succ_len[%d]", send_len, send_succ_len);

        //收包
        char recv_buf[sizeof(BinlogSyncStream)];
        int recv_len = 0;
        while(recv_len != sizeof(BinlogSyncStream))
        {
            int _len = thread->m_master_sock.recv(recv_buf + recv_len, sizeof(recv_buf) - recv_len);
            if(_len <= 0)
            {
                //TODO 收包出错，准备处理 --fix
                break;
            }
            recv_len += _len;
        }

        //解析头部，得到streamSize
        BinlogSyncStream* pStream = (BinlogSyncStream*)recv_buf;
        COMM_LOG(Logger::DEBUG, "recv head end, recv_len[%d] streamSize[%d]", recv_len, pStream->streamSize);
        if(pStream->error != BinlogSyncStream::NoError )
        {
            COMM_LOG(Logger::ERROR, "stream error, error[%d] errorMsg[%s]", pStream->error, pStream->errorMsg);
            continue;
        }
        //循环接收主体部分LogItem信息
        int bodyLen = 0;
        int bufLen = 32*1024*1024;
        while(bodyLen != pStream->streamSize)
        {
            int _len = thread->m_master_sock.recv( thread->m_net_buf + bodyLen, bufLen - bodyLen );
            if(_len <= 0)
            {
                //TODO 收包出错，准备处理 --fix
                break;
            }
            bodyLen += _len;
        }

        //开始处理logitem
        int i = 0;
        for (BinLog::LogItem* item = pStream->firstLogItem();
             i < pStream->logItemCount; item = pStream->nextLogItem(item), ++i)
        {
            LevelDbCluster *dbCluster = thread->server->getDbCluster();
            switch(item->type)
            {
                case BinLog::LogItem::DEL:
                {
                    //TODO 写操作
                    COMM_LOG(Logger::DEBUG, "sync del cmd, key[%s]", item->keyBuffer());
                    break;
                }
                case BinLog::LogItem::SET:
                {
                    //TODO 写操作
                    COMM_LOG(Logger::DEBUG, "sync set cmd, key[%s] value[%s]", item->keyBuffer(), item->valueBuffer());
                    break;
                }
                default:
                {
                    COMM_LOG(Logger::ERROR, "sync error cmd, type[%d]", item->type);
                    break;
                }
            }
        }

        //记录一下日志同步偏移量
        char posBuf[20] = {0};
        sprintf(posBuf, "%d", pStream->lastUpdatePos);
        thread->m_master_sync_info.clear();
        thread->m_master_sync_info.push_back(pStream->srcFileName);
        thread->m_master_sync_info.push_back(posBuf);
        TextConfigFile::write(thread->m_binlog_index_file, thread->m_master_sync_info);
        COMM_LOG(Logger::DEBUG, "write binlog index, srcFileName[%s] posBuf[%d]", pStream->srcFileName, pStream->lastUpdatePos);

        sleep(60); //休眠
    }
}


void SyncThread::start()
{
    pthread_create(&m_thread_id,NULL,run, this);
}

