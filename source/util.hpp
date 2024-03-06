#pragma once
#include <cstdio>
#include <ctime>

#include <fstream>
#include <memory>
#include <string>
#include <sstream>

#include <mysql/mysql.h>
#include <jsoncpp/json/json.h>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

typedef websocketpp::server<websocketpp::config::asio> wsserver_t;

/*************************这里是一个日志的宏，用于简单的打印日志*****************************/
/**
 * 使用规则：
 * normal信息：   INF_LOG(format, ...)
 * debug信息：    DBG_LOG(format, ...)
 * error信息：    ERR_LOG(format, ...)
 */

// 日志等级
#define INF 0
#define DBG 1
#define ERR 2

#define POS stdout // 日志打印的位置

// 默认的日志等级
#define DEFAULT_LOG_LEVEL INF

// [时间 文件名-行数]日志内容
#define LOG(level, format, ...)                                                             \
    do                                                                                      \
    {                                                                                       \
        if (level < DEFAULT_LOG_LEVEL)                                                      \
            break;                                                                          \
        time_t t = time(NULL);                                                              \
        struct tm *lt = localtime(&t);                                                      \
        char buf[31] = {0};                                                                 \
        strftime(buf, 31, "%H:%M:%S", lt);                                                  \
        fprintf(stdout, "[%s %s-%d] " format "\n", buf, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)

/*************************这里是一个mysql工具类，用于封装mysql的一些C接口*****************************/

class MysqlUtil
{
public:
    // 构建mysql句柄，连接mysql服务，设置字符集，选择要使用的数据库
    static MYSQL *mysql_create(const std::string &host, const std::string &user,
                               const std::string &passwd, const std::string db, uint16_t port = 3306)
    {
        // 1. 构建mysql句柄
        MYSQL *mysql = mysql_init(nullptr);
        if (mysql == nullptr)
        {
            ERR_LOG("构建mysql句柄出错: %s", mysql_error(mysql));
            return nullptr;
        }
        // 2. 连接mysql服务器
        if (mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str(), db.c_str(), port, nullptr, 0) == nullptr)
        {
            ERR_LOG("连接mysql服务出错: %s", mysql_error(mysql));
            return nullptr;
        }
        // 3. 设置字符集
        if (mysql_set_character_set(mysql, "utf8") != 0)
        {
            ERR_LOG("设置字符集出错: %s", mysql_error(mysql));
            return nullptr;
        }
        return mysql;
    }
    // 让mysql句柄来执行指定的sql语句
    static bool mysql_exec(MYSQL *mysql, const char *sql)
    {
        if (mysql_query(mysql, sql) != 0)
        {
            ERR_LOG("sql语句: %s 执行出错, %s ", sql, mysql_error(mysql));
            return false;
        }
        // INF_LOG("sql语句: %s 执行成功", sql);
        return true;
    }
    // 回收mysql句柄
    static void mysql_destory(MYSQL *mysql)
    {
        if (mysql != nullptr)
        {
            mysql_close(mysql);
        }
    }
};

/*************************这里是一个Json工具类，用于封装Json的序列化和反序列化*****************************/

class JsonUtil
{
public:
    // 序列化
    static bool serialize(const Json::Value &root, std::string *str)
    {
        std::stringstream ss;
        std::unique_ptr<Json::StreamWriter> sw(Json::StreamWriterBuilder().newStreamWriter());
        int ret = sw->write(root, &ss);
        if (ret != 0)
        {
            ERR_LOG("json serialize fail");
            return false;
        }
        *str = ss.str();
        return true;
    }
    static bool unserialize(const std::string &str, Json::Value &root)
    {
        std::string err;
        std::unique_ptr<Json::CharReader> cr(Json::CharReaderBuilder().newCharReader());
        bool ret = cr->parse(str.c_str(), str.c_str() + str.size(), &root, &err);
        if (ret == false)
        {
            ERR_LOG("json unserialize fail: %s", err.c_str());
            return false;
        }
        return true;
    }

    // 反序列化
};

// 字符串分割工具
class StringUtil
{
public:
    static int spilt(const std::string &src, const std::string &sep, std::vector<std::string> &res)
    {
        size_t pos, idx = 0;
        while (idx < src.size())
        {
            pos = src.find(sep, idx);
            if (pos == std::string::npos)
            {
                res.push_back(src.substr(idx));
                break;
            }
            if (pos == idx)
            {
                idx += sep.size();
                continue;
            }
            res.push_back(src.substr(idx, pos - idx));
            idx = pos + sep.size();
        }
        return res.size();
    }
};

// 文件读取工具
class FilereadUtil
{
public:
    static bool read(const std::string &filename, std::string &body)
    {
        // 1. 打开文件
        std::ifstream out(filename, std::ios::in | std::ios::binary);
        if (!out.is_open())
        {
            ERR_LOG("open file fail:%s", filename.c_str());
            return false;
        }
        // 2. 获取文件大小
        // 这里获取文件大小采用的方法是移动指针到结尾，然后查看当前位置的偏移量的方式
        // 如果设计传入的filename一定是绝对路径的话，可以使用系统调用stat
        size_t fsize = 0;
        out.seekg(0, std::ios::end);
        fsize = out.tellg();
        out.seekg(0, std::ios::beg);

        // 3. 读取文件内容
        body.resize(fsize);
        out.read(const_cast<char *>(body.c_str()), fsize);
        if (out.good() == false)
        {
            ERR_LOG("read %s file content fail", filename.c_str());
        }
        out.close();
        return true;
    }
};
