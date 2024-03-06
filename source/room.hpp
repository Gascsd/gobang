#pragma once

#include <memory>

#include "db.hpp"
#include "online.hpp"
#include "util.hpp"

#define BOARD_ROW 19
#define BOARD_COL 19

typedef enum
{
    GAME_START,
    GAME_OVER
} RoomStatus_t;
typedef enum
{
    BLACK = 1,
    WHITE = 2
} Color;

class Room
{
public:
    Room(uint64_t room_id, UserTable *tb_user, OnlineManager *online_user)
        : _room_id(room_id), _status(GAME_START), _player_count(0), _tb_user(tb_user), _online_user(online_user), _board(BOARD_ROW, std::vector<int>(BOARD_COL, 0))
    {
        DBG_LOG("%lu 房间创建成功", _room_id);
    }
    ~Room()
    {
        DBG_LOG("%lu 房间销毁成功", _room_id);
    }
    uint64_t id() { return _room_id; }
    RoomStatus_t status() { return _status; }
    int player_count() { return _player_count; }
    void add_white_user(uint64_t uid)
    {
        _white_id = uid;
        ++_player_count;
    }
    void add_black_user(uint64_t uid)
    {
        _black_id = uid;
        ++_player_count;
    }
    uint64_t get_white_user() { return _white_id; }
    uint64_t get_black_user() { return _black_id; }

    /*走棋的requset json
    {
        "optype" : "put_chess",     // put_chess表⽰当前请求是下棋操作
        "room_id" : 222,            // room_id 表⽰当前动作属于哪个房间
        "uid" : 1,                  // 当前的下棋操作是哪个⽤⼾发起的
        "row" : 3,                  // 当前下棋位置的⾏号
        "col" : 2                   // 当前下棋位置的列号
    }
    */
    /*走棋失败的response json
    {
        "optype" : "put_chess",
        "result" : false
        "reason" : "⾛棋失败具体原因...."
    }
    */
    /*走棋成功的response json
    {
        "optype" : "put_chess",
        "result" : true,
        "reason" : "有人退出!" / "对⽅/⼰⽅ 恭喜你，游戏胜利/虽败犹荣！",
        "room_id" : 222,
        "uid" : 1,
        "row" : 3,
        "col" : 2,
        "winner" : 0 // 0-未分胜负， !0-已分胜负 (uid是谁，谁就赢了)
    }
    */

    // 下棋
    Json::Value handle_chess(Json::Value &req)
    {
        Json::Value resp;
        resp["optype"] = "put_chess";
        // 2. 判断是否有用户退出房间
        int row = req["row"].asInt();
        int col = req["col"].asInt();
        int cur_uid = req["uid"].asInt64();
        if (_player_count == 1)
        {
            // 当前一定有1人退出
            uint64_t winner = 0;
            if (_online_user->in_game_room(_white_id) == false)
                winner = _black_id; // 白方退出
            else
                winner = _white_id;
            resp["result"] = true;
            resp["reason"] = "对方退出，恭喜你赢了!";
            resp["uid"] = cur_uid;
            resp["row"] = row;
            resp["col"] = col;
            resp["winner"] = Json::Value::UInt64(winner);

            return resp;
        }
        if (_player_count == 0)
        {
            DBG_LOG("当前房间没有人了，销毁房间");
            this->~Room();
        }
        // 3. 获取走棋位置，判断是否合法
        if (_board[row][col] != 0)
        {
            // 走棋不合法
            resp["result"] = false;
            resp["reason"] = "走棋不合法，当前位置有棋啦!";
            return resp;
        }
        Color cur_color = cur_uid == _white_id ? WHITE : BLACK;
        _board[row][col] = cur_color;
        resp["result"] = true;
        resp["room_id"] = Json::Value::UInt64(_room_id);
        resp["uid"] = cur_uid;
        resp["row"] = row;
        resp["col"] = col;
        // 4. 判断当前下棋人是否胜利
        uint64_t winner_id = check_win(row, col, cur_color);
        if (winner_id == 0) // 目前无人胜利
        {
            resp["reason"] = "游戏继续";
            resp["winner"] = 0;
        }
        else // 游戏结束
        {
            // 设置json内容
            resp["reason"] = "恭喜你，赢了";
            resp["winner"] = Json::Value::UInt64(winner_id);
        }
        return resp;
    }

    // 聊天
    /* 这里是聊天req的json
    {
        "optype": "chat",
        "room_id": 222,
        "uid": 1,
        "message": "赶紧点"
    }
    {
        "optype": "chat",
        "result": false
        "reason": "聊天失败具体原因....⽐如有敏感词..."
    }
    {
        "optype": "chat",
        "result": true,
        "room_id": 222,
        "uid": 1,
        "message": "赶紧点"
    } */
    Json::Value handle_chat(Json::Value &req)
    {
        Json::Value json_resp;
        json_resp["optype"] = "chat";

        // 2. 检测消息中的敏感词并脱敏
        std::string msg = req["message"].asString();
        replace_sensitive(msg);
        // 3. 返回消息json_resp
        json_resp["result"] = true;
        json_resp["room_id"] = req["room_id"];
        json_resp["uid"] = req["uid"];
        json_resp["message"] = msg;
        return json_resp;
    }
    // 玩家退出
    void handle_exit(uint64_t uid)
    {
        // 如果是下棋中退出，那么对方胜利，如果是下棋后退出，那么是正常
        Json::Value json_resp;
        if(_status == GAME_START)
        {
            //游戏中
            uint64_t winner_id = (uid == _white_id ? _black_id : _white_id);
            uint64_t loser_id = uid;
            // json操作
            json_resp["optype"] = "put_chess";
            json_resp["result"] = true;
            json_resp["reason"] = "对方掉线，不战而胜";
            json_resp["room_id"] = Json::Value::UInt64(_room_id);
            json_resp["uid"] = Json::Value::UInt64(uid);
            json_resp["row"] = -1;
            json_resp["col"] = -1;
            json_resp["winner"] = Json::Value::UInt64(winner_id);
            // 数据库操作
            _tb_user->win(winner_id);
            _tb_user->lose(loser_id);
            _status = GAME_OVER;
        }
        broadcast(json_resp);
        _player_count--;
    }
    // 一个总的请求函数，里面根据请求分别调用不同的操作
    void handle_request(Json::Value &req)
    {
        Json::Value json_resp;
        // 1. 检测房间号正确性
        if (req["room_id"].asUInt64() != _room_id)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "房间号不匹配!";
            return broadcast(json_resp);
        }
        json_resp["room_id"] = Json::Value::UInt64(_room_id);

        // 2. 根据不同的请求类型调用不同的函数
        if (req["optype"].asString() == "put_chess")
        {
            json_resp = handle_chess(req);
            if (json_resp["winner"].asInt64() != 0)
            {
                // 这里就是出现了赢家
                uint64_t winner_id = json_resp["winner"].asInt64();
                uint64_t loser_id = (winner_id == _white_id ? _black_id : _white_id);
                // 更新数据库
                _tb_user->win(winner_id);
                _tb_user->lose(loser_id);
                _status = GAME_OVER;
            }
        }
        else if (req["optype"].asString() == "chat")
        {
            json_resp = handle_chat(req);
        }
        else
        {
            json_resp["optype"] = req["optype"].asString();
            json_resp["result"] = false;
            json_resp["reason"] = "出现未知错误!";
        }
        return broadcast(json_resp);
    }

    // 广播rsp信息给整个房间的用户
    void broadcast(Json::Value &rsp)
    {
        // 1. 序列化相应信息
        std::string body;
        JsonUtil::serialize(rsp, &body);
        // 2. 广播相应信息
        wsserver_t::connection_ptr wconn = _online_user->get_conn_from_room(_white_id);
        if (wconn.get() != nullptr)
        {
            wconn->send(body);
        }
        wsserver_t::connection_ptr bconn = _online_user->get_conn_from_room(_black_id);
        if (bconn.get() != nullptr)
        {
            bconn->send(body);
        }
    }

private:
    // 替换敏感词
    void replace_sensitive(std::string &originalStr)
    {
        std::vector<std::string> sensitives = {"垃圾", "废物"};
        for (auto &str : sensitives)
        {
            auto pos = originalStr.find(str);
            while (pos != std::string::npos)
            {
                originalStr.replace(pos, str.size(), std::string(str.size(), '*'));
                pos = originalStr.find(str, pos + str.size());
            }
        }
    }

    /**
     * row和col 是下棋的行和列
     * color ：棋子颜色
     * row_off 和 col_off 是偏移量，确定了寻找的方向
     */
    bool count_same(int row, int col, int row_off, int col_off, Color color)
    {
        int count = 1;
        // 正向
        int serach_row = row + row_off;
        int serach_col = col + col_off;
        while (serach_row >= 0 && serach_row <= BOARD_ROW && serach_col >= 0 && serach_col <= BOARD_ROW &&
               _board[serach_row][serach_col] == color)
        {
            count++; // 同色棋子个数+1
            serach_row += row_off;
            serach_col += col_off;
        }
        // 反向
        serach_row = row - row_off;
        serach_col = col - col_off;
        while (serach_row >= 0 && serach_row <= BOARD_ROW && serach_col >= 0 && serach_col <= BOARD_ROW &&
               _board[serach_row][serach_col] == color)
        {
            count++; // 同色棋子个数+1
            serach_row -= row_off;
            serach_col -= col_off;
        }
        return count >= 5;
    }
    uint64_t check_win(int row, int col, Color color)
    {
        // 四个方向检测是否出现5个连起来的同色棋子，如果有就说明有人胜利
        if (count_same(row, col, 0, 1, color) || count_same(row, col, 1, 0, color) ||
            count_same(row, col, -1, 1, color) || count_same(row, col, -1, -1, color))
        {
            return color == WHITE ? _white_id : _black_id;
        }
        return 0;
    }

private:
    uint64_t _room_id;                    // 房间号
    RoomStatus_t _status;                 // 在线状态
    int _player_count;                    // 当前房间人数
    uint64_t _white_id;                   // 白色持方的id
    uint64_t _black_id;                   // 黑色持方的id
    UserTable *_tb_user;                  // UserTable句柄
    OnlineManager *_online_user;          // 在线用户句柄
    std::vector<std::vector<int>> _board; // 当前房间的棋盘
};

using room_ptr = std::shared_ptr<Room>;

class RoomManager
{
public:
    RoomManager(UserTable *ut, OnlineManager *om)
        : _next_rid(1), _utb(ut), _om(om)
    {
        DBG_LOG("房间管理模块初始化成功");
    }
    ~RoomManager()
    {
        DBG_LOG("房间管理模块销毁成功");
    }
    // 用用户uid1和uid2创建一个房间
    room_ptr createRoom(uint64_t uid1, uint64_t uid2)
    {
        // 1. 首先判断两个用户是否还在大厅
        if(_om->in_game_hall(uid1) == false)
        {
            DBG_LOG("%lu 不在大厅，创建房间失败", uid1);
            return room_ptr();
        }
        if(_om->in_game_hall(uid2) == false)
        {
            DBG_LOG("%lu 不在大厅，创建房间失败", uid2);
            return room_ptr();
        }
        // 2. 如果都在大厅的话创建一个房间
        std::lock_guard<std::mutex> lck(_mutex); // 分配房间号的过程要保证线程安全
        room_ptr rp(new Room(_next_rid, _utb, _om));
        // 3. 将用户uid1和uid2添加到房间中，添加uid和rid的映射
        rp->add_black_user(uid1);
        rp->add_white_user(uid2);
        // _om->exit_game_hall(uid1);
        // _om->exit_game_hall(uid2);
        _users.insert(std::make_pair(uid1, _next_rid));
        _users.insert(std::make_pair(uid2, _next_rid));
        // 4. 将房间管理起来
        _rooms.insert(make_pair(_next_rid, rp));
        // std::cout << "在房间管理中添加房间id为 " << _next_rid << "的房间" << _rooms[_next_rid] << "成功" << std::endl;
        // 5. 返回房间信息指针
        _next_rid++;
        return rp;
    }
    // 通过房间id获取房间指针
    room_ptr get_room_by_rid(uint64_t rid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto ret = _rooms.find(rid);
        if(ret == _rooms.end())
        {
            DBG_LOG("不存在该房间号为 %lu 的房间", rid);
            return room_ptr();
        }
        return ret->second;
    }
    // 通过用户id获取房间指针
    room_ptr get_room_by_uid(uint64_t uid)
    {
        auto ret1 = _users.find(uid);
        if(ret1 == _users.end())
        {
            DBG_LOG("没有存在用户id %lu 的房间", uid);
            return room_ptr();
        }
        auto rid = ret1->second;
        room_ptr rp = get_room_by_rid(rid);
        // auto ret2 = _rooms.find(rid);
        // if(ret2 == _rooms.end())
        // {
        //     DBG_LOG("不存在该房间号为 %lu 的房间", rid);
        //     return room_ptr();
        // }
        // return ret2->second;
        return rp;
    }
    //  通过rid销毁房间 
    void remove_room(uint64_t rid)
    {
        // 1. 通过rid获取房间信息
        room_ptr rp = get_room_by_rid(rid);
        if(rp.get() == nullptr)
        {
            return;
        }
        // 2. 获取到房间内用户id
        uint64_t uid1 = rp->get_white_user();
        uint64_t uid2 = rp->get_black_user();
        // 3. 移除房间内所有用户
        std::lock_guard<std::mutex> lck(_mutex); // 加锁保护users和rooms的操作
        _users.erase(uid1);
        _users.erase(uid2);
        // 4. 销毁房间（从_room中移除智能指针会自己释放）
        _rooms.erase(rid);
    }
    // 移除房间中的指定用户
    void remove_room_user(uint64_t uid)
    {
        // 1. 获取uid对应的rp
        room_ptr rp = get_room_by_uid(uid);
        if(rp.get() == nullptr)
        {
            return;
        }
        // 2. 从房间中移除uid
        rp->handle_exit(uid);
        // 3. 如果房间中没有用户了就销毁房间
        // std::cout << "房间：" << rp->id() << " 内用户个数为：" << rp->player_count() << " 个" << std::endl;
        if(rp->player_count() == 0)
        { 
            remove_room(rp->id());
        }
    }
private:
    uint64_t _next_rid; // 唯一的房间号
    std::mutex _mutex;  // 互斥锁保护分配房间号的过程
    UserTable *_utb;    // 用户信息句柄
    OnlineManager *_om; // 在线用户管理句柄
    std::unordered_map<uint64_t, room_ptr> _rooms; // 房间号和房间指针的映射
    std::unordered_map<uint64_t, uint64_t> _users; // 用户id和房间id的映射
};
