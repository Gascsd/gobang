#pragma once

#include <unordered_map>

#include "util.hpp"

#define SESSION_TIMEOUT 30000
#define SESSION_FOREVER -1

typedef enum
{
    UNLOGIN,
    LOGIN
} sstatus_t;

class Session
{
public:
    Session(uint64_t ssid) : _ssid(ssid) { DBG_LOG("SESSION %p 被创建", this); }
    ~Session() { DBG_LOG("SESSION %p 被释放", this); }
    uint64_t ssid() { return _ssid; }
    void set_user(uint64_t uid) { _uid = uid; }
    void set_status(sstatus_t status) { _status = status; }
    void set_timer(const wsserver_t::timer_ptr &tp) { _tp = tp; }
    uint64_t get_user() { return _uid; }
    sstatus_t get_status() { return _status; }
    wsserver_t::timer_ptr &get_timer() { return _tp; }
    bool is_login() { return _status == LOGIN; }

private:
    uint64_t _ssid;            // session的标识符
    uint64_t _uid;             // session对用的用户id
    sstatus_t _status;         // 用户状态
    wsserver_t::timer_ptr _tp; // 定时器
};

using session_ptr = std::shared_ptr<Session>;

class SessionManager
{
public:
    SessionManager(wsserver_t *server) : _next_ssid(1), _server(server) { DBG_LOG("session 管理器初始化完毕"); }
    ~SessionManager() { DBG_LOG("session 管理器销毁完毕"); }
    // 在本项目中用户只需要进行操作就一定要登录，所以没有不登陆的session状态，但是有些网站是允许一些操作是未登录的session
    session_ptr createSession(uint64_t uid, sstatus_t status)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        session_ptr sp(new Session(_next_ssid));
        sp->set_user(uid);
        sp->set_status(status);
        _sessions.insert(make_pair(_next_ssid, sp));
        ++_next_ssid;
        return sp;
    }
    session_ptr getSessionBySsid(uint64_t ssid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        auto it = _sessions.find(ssid);
        if(it == _sessions.end())
        {
            return session_ptr();
        }
        return it->second;
    }
    void removeSession(uint64_t ssid)
    {
        std::lock_guard<std::mutex> lck(_mutex);
        _sessions.erase(ssid);
    }
    void appendSession(session_ptr ssp)
    {
        std::lock_guard<std::mutex> lck(_mutex); // 这个操作需要加锁保证线程安全
        _sessions.insert(std::make_pair(ssp->ssid(), ssp));
    }
    void setExpirationTime(uint64_t ssid, int ms)
    {
        // 依赖于websocketpp的定时器来完成session生命周期的管理（设定指定时间之后执行删除session）
        // 前提：session在创建的时候是“永久存在的”因为没有设置销毁时间
        session_ptr ssp = getSessionBySsid(ssid);
        if(ssp.get() == nullptr) return; //没有对应ssid的session,就直接return，不用设置

        wsserver_t::timer_ptr tp = ssp->get_timer();
        // 1. 在用户登录期间使用的是http短连接，此时就需要给session设置销毁（指定时间无通信）的定时任务（永久存在->设置定时销毁）
        if (tp.get() == nullptr && ms != SESSION_FOREVER)
        {
            wsserver_t::timer_ptr tp =  _server->set_timer(ms, 
                std::bind(&SessionManager::removeSession, this, ssid));
            ssp->set_timer(tp);
        }
        // 2. 在用户进入游戏大厅/游戏房间后，使用的应该是websocket长连接，此时session就需要永久存在（永久存在->永久存在）
        else if(tp.get() == nullptr && ms == SESSION_FOREVER)
        {
            return; // tp中没有定时器，也不用重新设置定时器
        }
        // 3. 在用户使用的协议从http切换到websocket的时候，需要将session设置为永久存在（设置定时销毁->永久存在）
        else if(tp.get() != nullptr && ms == SESSION_FOREVER)
        {
            // 删除之前的定时任务
            tp->cancel(); // 但是这里调用cancel之后，当OS意识到有这个操作的时候，将会触发已经设定过的定时任务执行一次
            // 因此我们在cancel之后需要在session管理器中重新添加这个session

            // 由于上面调用cancel的时候并不是调用立即执行，而是等待OS意识到这个操作的时候才会执行（异步的）
            // 所以我们不能在下面的代码中立即执行重新添加，否则就会出现“先添加了一次session，然后再执行的cancel”的情况
            // 所以这里我们使用定时任务的方式让OS重新添加（这个定时为0即可），此时由于两个操作都是异步的，OS肯定是先是一道cancel然后再意识到重新添加操作
            // 所以就不会出现这个问题，
            ssp->set_timer(wsserver_t::timer_ptr()); // 设置永久存在的时候就是要让其tp为nullptr
            _server->set_timer(0, std::bind(&SessionManager::appendSession, this, ssp));
        }
        // 4. 用户在使用http短连接的时候，在指定时间内产生了通信，要刷新session设置的定时时间（设置定时销毁->重置定时销毁时间）
        else if(tp.get() != nullptr && ms != SESSION_FOREVER)
        {
            // 取消之前的定时任务
            tp->cancel();
            ssp->set_timer(wsserver_t::timer_ptr());
            // 用定时器给session管理器中重新添加一个定时任务
            _server->set_timer(0, std::bind(&SessionManager::appendSession, this, ssp));
            // 用定时器设置定时销毁时间
            wsserver_t::timer_ptr tp = _server->set_timer(ms, std::bind(&SessionManager::removeSession, this, ssp->ssid()));
            ssp->set_timer(tp); // 重新设置session的定时器
        }
    }

private:
    uint64_t _next_ssid; // 
    std::mutex _mutex;
    std::unordered_map<uint64_t, session_ptr> _sessions;
    wsserver_t *_server;
};
