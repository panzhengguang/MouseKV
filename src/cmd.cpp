#include "cmd.h"
#include "mouse.h"
#include <sstream>
#include <string>
using namespace std;


class Mouse;

//装换为大写
void strToUpper(string& str)
{
    for (size_t i = 0; i < str.size(); ++i)
    {
        str[i] = static_cast<char>(toupper(str[i]));
    }
}


//下面定义GET/SET的处理函数
void MouseGetCmdHandler(Context* c)
{
	if(c->vec_req_params.size() != 2)
	{
	    c->setFinishedState(Context::WrongNumberOfArguments);
		return;
	}
    //处理逻辑
    Mouse *_mouse_server = (Mouse*)c->server;
	string value;
	bool get_ret = _mouse_server->getDbCluster()->getValue(c->vec_req_params[1],value);
	if(get_ret)
    {
        c->sendBuff.appendFormatString("$%d\r\n", value.size());
        c->sendBuff.append(value.data(),value.size());
        c->sendBuff.append("\r\n");
        c->setFinishedState(Context::RequestFinished);
    } else
        c->setFinishedState(Context::RequestError);
}


void MouseSetCmdHandler(Context *c)
{
	if(c->vec_req_params.size() != 3)
	{
	    c->setFinishedState(Context::WrongNumberOfArguments);
		return;
	}
	//先简单打印一下kv，然后回包ok
	COMM_LOG(Logger::DEBUG,"key[%s],value[%s]\n",c->vec_req_params[1].c_str(), c->vec_req_params[2].c_str());
	//处理逻辑
    Mouse *_mouse_server = (Mouse*)c->server;
    bool set_ret = _mouse_server->getDbCluster()->setValue(c->vec_req_params[1],c->vec_req_params[2]);
	if(set_ret)
    {
        c->sendBuff.append("+OK\r\n");
        c->setFinishedState(Context::RequestFinished);
    } else
        c->setFinishedState(Context::RequestError);
}


//删除操作
void MouseDelCmdHandler(Context* c)
{
    if(c->vec_req_params.size() != 2)
    {
        c->setFinishedState(Context::WrongNumberOfArguments);
        return;
    }
    //处理逻辑
    COMM_LOG(Logger::DEBUG,"MouseDelCmdHandler, para size[%uz],  para1[%s], para2[%s]",
        c->vec_req_params[0].c_str(),c->vec_req_params[1].c_str());

    Mouse *_mouse_server = (Mouse*)c->server;
    bool del_ret = _mouse_server->getDbCluster()->delKey(c->vec_req_params[1]);
    if(del_ret)
    {
        c->sendBuff.append("+OK\r\n");
        c->setFinishedState(Context::RequestFinished);
    } else
        c->setFinishedState(Context::RequestError);
}


//下面是CmdTable的实现
bool CmdTable::AddCmd(string cmd, CmdHandler handler)
{
    COMM_LOG(Logger::ERROR,"AddCmd, cmd[%s]\n",cmd.c_str());
	m_map_cmd_handler[cmd] = handler;
}


bool CmdTable::GetCmdHandler(const string & cmd, CmdHandler& handler)
{
	COMM_LOG(Logger::DEBUG,"GetCmdHandler, cmd[%s]\n",cmd.c_str());
	//这里统一转换为大写命令字
    string str_cmd = cmd;
    strToUpper(str_cmd);
    if(m_map_cmd_handler.count(str_cmd))
    {
    	handler = m_map_cmd_handler[str_cmd];
		return true;
    }
	return false;
}


CmdTable* CmdTable::instance(void)
{
	static CmdTable* table = NULL;
	if (!table) 
	{
	  table = new CmdTable;
	  table->AddCmd("SET", MouseSetCmdHandler);
	  table->AddCmd("GET", MouseGetCmdHandler);
	  table->AddCmd("DEL", MouseDelCmdHandler);
	}
	return table;

}



