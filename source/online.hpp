#pragma once

#include <mutex>
#include <unordered_map>

#include "util.hpp"

class OnlineManager
{
public:
    void enter_game_hall(uint64_t uid, wsserver_t::connection_ptr &conn)
    {
        std::lock_guard<std::mutex> lck(_mutex); // 加锁，保证线程安全
        _game_hall.insert(make_pair(uid, conn));
    }
    void enter_game_room(uint64_t uid, wsserver_t::connection_ptr &conn)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        _game_room.insert(make_pair(uid, conn));
    }
    void exit_game_hall(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        _game_hall.erase(uid);
    }
    void exit_game_room(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        _game_room.erase(uid);
    }
    bool in_game_hall(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex); // 这里考虑是不是可以不加锁，只有读取，没有写入！！！！
        auto pos = _game_hall.find(uid);
        if (pos == _game_hall.end())
        {
            return false;
        }
        return true;
    }
    bool in_game_room(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        auto pos = _game_room.find(uid);
        if (pos == _game_room.end())
        {
            return false;
        }
        return true;
    }
    // 这里如果没找到对应uid的用户就返回nullptr，所以使用前需要检测
    wsserver_t::connection_ptr get_conn_from_hall(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        auto pos = _game_hall.find(uid);
        if (pos == _game_hall.end())
        {
            return wsserver_t::connection_ptr();
        }
        return pos->second;
    }
    wsserver_t::connection_ptr get_conn_from_room(uint64_t uid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        auto pos = _game_room.find(uid);
        if (pos == _game_room.end())
        {
            return wsserver_t::connection_ptr();
        }
        return pos->second;
    }

private:
    std::mutex _mutex;                                                   // 互斥锁保证线程安全
    std::unordered_map<uint64_t, wsserver_t::connection_ptr> _game_hall; // 游戏大厅用户管理
    std::unordered_map<uint64_t, wsserver_t::connection_ptr> _game_room; // 游戏房间用户管理
};
