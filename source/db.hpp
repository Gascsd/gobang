#pragma once

#include <cassert>
#include <mutex>

#include "util.hpp"

class UserTable
{
public:
    UserTable(const std::string &host, const std::string &user,
               const std::string &password, const std::string &db, uint16_t port = 3306)
    {
        _mysql = MysqlUtil::mysql_create(host, user, password, db, port);
        assert(_mysql != nullptr);
    }
    ~UserTable()
    {
        MysqlUtil::mysql_destory(_mysql);
        _mysql = nullptr;
    }
    // 注意，这里的函数没有控制输入的参数一定是username和password，在前端要实现数据校验！！！！

    // 注册时新增用户
    bool insert(Json::Value &user)
    {
#define DEFAULT_SOCRE 1000 // 默认的天梯分数值
#define ADD_SOCRE 30       // 每次胜利增加的天梯分数值
#define INSERT_USER "insert user values(null, '%s', password('%s'), %d, 0, 0);"

        Json::Value val;
        bool ret = select_by_name(user["username"].asCString(), val);
        if (ret == true)
        {
            DBG_LOG("user:%s is already exists", user["username"].asCString());
            return false;
        }
        char sql[4096] = {0};
        snprintf(sql, sizeof(sql) - 1, INSERT_USER, user["username"].asCString(), user["password"].asCString(), DEFAULT_SOCRE);
        ret = MysqlUtil::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            ERR_LOG("insert user info fail");
            return false;
        }
        return true;
    }

    // 登录时验证用户并把其他信息放进user中
    bool login(Json::Value &user)
    {
#define LOGIN_USER "select id,socre, total_count, win_count from user where username='%s' and password=password('%s');"
        char sql[4096] = {0};
        snprintf(sql, sizeof(sql) - 1, LOGIN_USER, user["username"].asCString(), user["password"].asCString());
        MYSQL_RES *res = NULL;
        {
            std::lock_guard<std::mutex> lck(_mutex);
            bool ret = MysqlUtil::mysql_exec(_mysql, sql);
            if (ret == false) // 没有查询到指定信息
            {
                DBG_LOG("user login fail");
                return false;
            }
            res = mysql_store_result(_mysql);
            if (res == NULL)
            {
                DBG_LOG("have no login user info");
                return false;
            }
        }
        int row_num = mysql_num_rows(res);
        if (row_num == 0)
        {
            DBG_LOG("have no login user info");
            return false;
        }
        if (row_num != 1)
        {
            DBG_LOG("The queried user information is not unique");
            return false;
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        user["id"] = std::stoi(row[0]);
        user["socre"] = std::stoi(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);
        mysql_free_result(res);
        return true;
    }

    // 使用username查询，如果查到将结果放进user中
    bool select_by_name(const std::string &username, Json::Value &user)
    {
#define SELECT_BY_NAME "select id,socre, total_count, win_count from user where username='%s';"
        char sql[4096] = {0};
        snprintf(sql, sizeof(sql) - 1, SELECT_BY_NAME, username.c_str());
        MYSQL_RES *res = NULL;
        {
            std::lock_guard<std::mutex> lck(_mutex);
            bool ret = MysqlUtil::mysql_exec(_mysql, sql);
            if (ret == false)
            {
                DBG_LOG("get user by name fail: %s", mysql_error(_mysql));
                return false;
            }
            res = mysql_store_result(_mysql);
            if (res == NULL)
            {
                DBG_LOG("The user:%s does not exist", username);
                return false;
            }
        }
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == NULL)
        {
            // DBG_LOG("The user:%s does not exist(res中没有内容，导致row为NULL)", username);
            return false;
        }
        user["id"] = Json::Value::UInt64(std::stoull(row[0]));
        user["username"] = username;
        user["socre"] = std::stoi(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);

        mysql_free_result(res);

        return true;
    }

    // 使用id查询，如果查到将结果放进user中
    bool select_by_id(uint64_t id, Json::Value &user)
    {
#define SELECT_BY_ID "select username,socre, total_count, win_count from user where id=%d;"
        char sql[4096] = {0};

        snprintf(sql, sizeof(sql) - 1, SELECT_BY_ID, id);
        MYSQL_RES *res = NULL;
        {
            std::lock_guard<std::mutex> lck(_mutex);
            bool ret = MysqlUtil::mysql_exec(_mysql, sql);
            if (ret == false)
            {
                DBG_LOG("get user by id fail %s", mysql_error(_mysql));
                return false;
            }
            res = mysql_store_result(_mysql);
            if (res == NULL)
            {
                DBG_LOG("The id:%d does not exist", id);
                return false;
            }
        }
        int row_num = mysql_num_rows(res);

        MYSQL_ROW row = mysql_fetch_row(res);
        if (row == NULL)
        {
            // DBG_LOG("The user:%s does not exist(res中没有内容，导致row为NULL)", username);
            return false;
        }
        user["id"] = Json::Value::UInt64(id); // 这里要使用UInt64转换为json的类型，否则会出现构造函数调用不明确
        user["username"] = row[0];
        user["socre"] = std::stoi(row[1]);
        user["total_count"] = std::stoi(row[2]);
        user["win_count"] = std::stoi(row[3]);
        mysql_free_result(res);
        return true;
    }

    // 给赢得人设置相关信息（天梯分数增加，总场数和胜场数增加）
    bool win(uint64_t id)
    {
#define ALTER_WIN "update user set socre=socre+%d,total_count=total_count+1,win_count=win_count+1 where id=%d;"
        char sql[4096] = {0};
        snprintf(sql, sizeof(sql) - 1, ALTER_WIN, ADD_SOCRE, id);
        bool ret = MysqlUtil::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            DBG_LOG("update win info fail: %s", mysql_error(_mysql));
            return false;
        }
        return true;
    }

    // 给输的人设置相关信息（胜场数增加）
    bool lose(uint64_t id)
    {
#define ALTER_LOSE "update user set total_count=total_count+1 where id=%d;"
        char sql[4096] = {0};
        snprintf(sql, sizeof(sql) - 1, ALTER_LOSE, id);
        bool ret = MysqlUtil::mysql_exec(_mysql, sql);
        if (ret == false)
        {
            DBG_LOG("update lose info fail: %s", mysql_error(_mysql));
            return false;
        }
        return true;
    }

private:
    MYSQL *_mysql;     // mysql的操作句柄
    std::mutex _mutex; // 互斥锁保护访问数据库的操作
};