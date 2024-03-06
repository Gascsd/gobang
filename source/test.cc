#include <iostream>
#include <vector>
#include <exception>

#include "room.hpp"
#include "session.hpp"
#include "matcher.hpp"
#include "server.hpp"

void MysqlUtil_test()
{
    MYSQL *mysql = MysqlUtil::mysql_create("127.0.0.1", "root", "zht1125x", "Rokuko");
    const char *sql = "insert stu values(null, 22, '王五', '男');";
    bool ret = MysqlUtil::mysql_exec(mysql, sql);
    if (ret == false)
    {
        std::cout << "sql执行出错" << std::endl;
    }
    MysqlUtil::mysql_destory(mysql);
}
void JsonUtil_test()
{
    Json::Value root;
    root["name"] = "张三";
    root["age"] = 19;
    root["sex"] = "男";
    std::string str;
    JsonUtil::serialize(root, &str);
    INF_LOG("%s", str.c_str());

    Json::Value root2;
    JsonUtil::unserialize(str, root2);
    std::cout << root2["name"].asString() << std::endl;
    std::cout << root2["age"].asInt() << std::endl;
    std::cout << root2["sex"].asString() << std::endl;
}
void StringUtil_test()
{
    std::string str = "123,张三,,...,,234,345,456";
    std::vector<std::string> ret;
    int n = StringUtil::spilt(str, ",", ret);

    for (auto &s : ret)
    {
        std::cout << s << std::endl;
    }
}
void FilereadUtil_test()
{
    std::string body;
    FilereadUtil::read("text.txt", body);
    std::cout << body << std::endl;
}

#define HOST "127.0.0.1"
#define USER "root"
#define DBNAME "Rokuko"
#define PASSWD "zht1125x"
#define PORT 3306

void db_test()
{
    UserTable ut("127.0.0.1", "root", "zht1125x", "Rokuko");
    Json::Value user;

    { // TEST insert
        user["username"] = "张三";
        user["password"] = "123123";
        ut.insert(user);
    }
    { // TEST login
        user["username"] = "张三";
        user["password"] = "123456";
        int ret = ut.login(user);
        if (ret == false)
            DBG_LOG("login fail");
        else
            DBG_LOG("login success");
    }
    { // TEST select_by_name
        std::string username = "李四";
        int ret = ut.select_by_name(username, user);
        if (ret == true)
        {
            DBG_LOG("found user:%s success", username.c_str());
            std::string info;
            JsonUtil::serialize(user, &info);
            std::cout << "序列化的信息为:" << info << std::endl;
        }
        else
            DBG_LOG("not found user:%s ", username.c_str());
    }
    { // TEST select_by_id
        uint64_t id = 1;
        int ret = ut.select_by_id(id, user);
        if (ret == true)
        {
            DBG_LOG("found id:%d success", id);
            std::string info;
            JsonUtil::serialize(user, &info);
            std::cout << "序列化的信息为:" << info << std::endl;
        }
        else
            DBG_LOG("not found id:%d ", id);
    }
    { // TEST win
        uint64_t id = 3;
        bool ret = ut.win(id);
        if (ret == false)
            DBG_LOG("win false");
        else
            DBG_LOG("win success");
    }
    { // TEST lose
        uint64_t id = 3;
        bool ret = ut.lose(id);
        if (ret == false)
            DBG_LOG("lose false");
        else
            DBG_LOG("lose success");
    }
}
void OnlineManager_test()
{
    OnlineManager om;
    wsserver_t::connection_ptr conn;
    om.enter_game_hall(1, conn);
    om.enter_game_hall(2, conn);
    om.enter_game_hall(3, conn);
    om.enter_game_room(4, conn);
    om.enter_game_room(5, conn);
    om.enter_game_room(6, conn);

    // om.exit_game_room(5);
    // om.exit_game_hall(3);
    uint64_t uid1 = 3;
    uint64_t uid2 = 5;

    // if(om.in_game_room(uid))
    //     DBG_LOG("uid:%d is in game room", uid);
    // else if(om.in_game_hall(uid))
    //     DBG_LOG("uid:%d is in game hall", uid);
    // else
    //     DBG_LOG("uid:%d is not in game hall and room", uid);

    wsserver_t::connection_ptr ret1 = om.get_conn_from_hall(uid1);
    if (ret1 != nullptr)
        DBG_LOG("get uid:%d connection_ptr success, %p", uid1, ret1);
    // else
    //  DBG_LOG("uid:%d connection_ptr is not exists", uid1);

    wsserver_t::connection_ptr ret2 = om.get_conn_from_room(uid2);
    if (ret2 != nullptr)
        DBG_LOG("get uid:%d connection_ptr success, %p", uid2, ret2);
    else
        DBG_LOG("uid:%d connection_ptr is not exists", uid2);
}

void Room_test()
{
    UserTable ut("127.0.0.1", "root", "zht1125x", "Rokuko");
    OnlineManager om;
    Room room1(10, &ut, &om);
}

void RomeManager_test()
{
    UserTable ut("127.0.0.1", "root", "zht1125x", "Rokuko");
    OnlineManager om;
    RoomManager rm(&ut, &om);
    room_ptr rp = rm.createRoom(10, 20);
}

void SessionManager_test()
{
    wsserver_t wss;
    SessionManager sm(&wss);
}

void MatchManager_test()
{
    // try
    // {
        UserTable ut("127.0.0.1", "root", "zht1125x", "Rokuko");
        OnlineManager om;
        RoomManager rm(&ut, &om);
        room_ptr rp = rm.createRoom(10, 20);
        MatchManager mm(&ut,&om, &rm);
    // }
    // catch (std::exception& e)
    // {
    //     std::cout << "异常被捕获" << std::endl;
    //     std::cout << e.what() << std::endl;
    // }
    // catch (...)
    // {
    //     std::cout << "未知异常" << std::endl;
    // }
}

void Server_test()
{
    Server svr("127.0.0.1", "root", "zht1125x", "Rokuko", 3306);
    svr.start(8081);
}

int main()
{
    Server_test();

    return 0;
}
