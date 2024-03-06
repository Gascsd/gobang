#pragma once

#include <list>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "util.hpp"
#include "room.hpp"
#include "session.hpp"

template<class T>
class MatchQueue
{
public:
    int size()
    {
        std::unique_lock<std::mutex> lck(_mutex);
        return _list.size();
    }
    bool empty() 
    {
        std::unique_lock<std::mutex> lck(_mutex);
        return _list.empty(); 
    }
    void wait()
    {
        std::unique_lock<std::mutex> lck(_mutex); // 这里使用unique_lock来管理是因为要让条件变量等待
        _cond.wait(lck);
    }
    void push(const T& data)
    {
        std::unique_lock<std::mutex> lck(_mutex);
        _list.push_back(data);
        _cond.notify_all();
    }
    bool pop(T &data)
    {
        std::unique_lock<std::mutex> lck(_mutex);
        if(_list.empty()) return false;
        data = _list.front();
        _list.pop_front();
        return true;
    }
    void remove(const T &data)
    {
        std::unique_lock<std::mutex> lck(_mutex);
        _list.remove(data);
    }
private:
    std::list<T> _list;
    std::mutex _mutex;
    std::condition_variable _cond;
};


class MatchManager
{
public:
    MatchManager(UserTable *ut, OnlineManager *om, RoomManager *rm) 
        : _rm(rm), _om(om), _ut(ut) 
        ,_th_normal(std::thread(&MatchManager::th_normal_entery,this))
        ,_th_high(std::thread(&MatchManager::th_high_entery,this))
        ,_th_super(std::thread(&MatchManager::th_super_entery,this))
    {
        DBG_LOG("游戏匹配模块初始化成功");
    }
    ~MatchManager()
    {
        DBG_LOG("游戏匹配模块销毁成功");
    }
    bool add(uint64_t uid)
    {
        // 1. 获取用户信息
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if(ret == false)
        { 
            DBG_LOG("获取玩家 %lu 信息失败", uid);
            return false;
        }
        // 根据分数放进不同档次的阻塞队列
        int score = user["score"].asInt();
        if(score < 2000)
            _q_normal.push(uid);
        else if(score >= 2000 && score < 3000)
            _q_high.push(uid);
        else
            _q_super.push(uid);
        return true;
    }
    bool del(uint64_t uid)
    {
        // 1. 获取用户信息
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if(ret == false)
        { 
            DBG_LOG("获取玩家 %lu 信息失败", uid);
            return false;
        }
        // 根据分数查找不同档次的阻塞队列
        int score = user["score"].asInt();
        if(score < 2000)
            _q_normal.remove(uid);
        else if(score >= 2000 && score < 3000)
            _q_high.remove(uid);
        else
            _q_super.remove(uid);
    }
private:
    void handle_match(MatchQueue<uint64_t> &mq)
    {
        while(true)
        {
            // 1. 人数小于2，就阻塞等待
            while(mq.size() < 2)
            {
                mq.wait(); // 阻塞等待，直到有人进入就唤醒，然后进行检测
            }
            // 2. 人数大于等于2，出队两个玩家
            uint64_t uid1, uid2;
            bool ret = mq.pop(uid1);
            if(ret ==false) continue;
            ret = mq.pop(uid2);
            if(ret ==false) { add(uid1); continue;} // 当uid1出队列之后如果uid2掉线了，让uid1还要重新入队列
            // 3. 校验出队的两个玩家的在线状态，如果有人掉线，就让在线的重新进队列等待匹配
            wsserver_t::connection_ptr conn1 = _om->get_conn_from_hall(uid1);
            if(conn1.get() == nullptr)
            {
                add(uid2);
                continue;
            }
            wsserver_t::connection_ptr conn2 = _om->get_conn_from_hall(uid2);
            if(conn2.get() == nullptr)
            {
                add(uid1);
                continue;
            }
            // 4. 为两个玩家创建房间，将玩家加入房间
            room_ptr rp = _rm->createRoom(uid1, uid2);
            if(rp.get() == nullptr)
            {
                // 如果房间没有创建成功，就让两个玩家重新进入匹配队列进行匹配
                add(uid1);
                add(uid2);
                continue;
            }
            // 5. 服务端建立房间，两个玩家进入房间成功后，给两个玩家响应
            Json::Value resp;
            resp["room_id"] = Json::UInt64(rp->id());
            resp["optype"] = "match_success";
            resp["result"] = true;
            std::string body;
            JsonUtil::serialize(resp, &body);
            conn1->send(body);
            conn2->send(body);
        }
    }
    void th_normal_entery() { return handle_match(_q_normal); }
    void th_high_entery() { return handle_match(_q_high); }
    void th_super_entery() { return handle_match(_q_super); }
private:
    MatchQueue<uint64_t> _q_normal;
    MatchQueue<uint64_t> _q_high;
    MatchQueue<uint64_t> _q_super;
    std::thread _th_normal;
    std::thread _th_high;
    std::thread _th_super;
    OnlineManager *_om;
    RoomManager *_rm;
    UserTable *_ut;
};
