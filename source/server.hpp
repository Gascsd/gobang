#pragma once

#include "db.hpp"
#include "matcher.hpp"
#include "online.hpp"
#include "room.hpp"
#include "session.hpp"
#include "util.hpp"

#define WEBROOT "./webroot"

class Server
{
public:
    Server(const std::string &host, const std::string &user, const std::string &password,
           const std::string &db, uint16_t port, const std::string &webroot = WEBROOT)
        : _ut(host, user, password, db, port), _rm(&_ut, &_om), _sm(&_wssvr), _mm(&_ut, &_om, &_rm), _web_root(webroot)
    {
        _wssvr.set_access_channels(websocketpp::log::alevel::none); // 设置成为禁止打印所有日志
        _wssvr.init_asio();
        _wssvr.set_reuse_addr(true);
        _wssvr.set_http_handler(std::bind(&Server::http_callback, this, std::placeholders::_1));
        _wssvr.set_open_handler(std::bind(&Server::wsopen_callback, this, std::placeholders::_1));
        _wssvr.set_close_handler(std::bind(&Server::wsclose_callback, this, std::placeholders::_1));
        _wssvr.set_message_handler(std::bind(&Server::wsmsg_callback, this, std::placeholders::_1, std::placeholders::_2));
    }
    ~Server()
    {
    }
    void start(int port)
    {
        _wssvr.listen(port);
        _wssvr.start_accept();
        _wssvr.run();
    }

private:
    void file_handle(wsserver_t::connection_ptr &conn) // 静态页面获取请求
    {
        // 1. 获取uri静态资源路径
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        std::string real_path = _web_root + uri;
        if (real_path.back() == '/')
        {
            real_path += "login.html";
        }
        Json::Value resp;
        std::string body;
        bool ret = FilereadUtil::read(real_path, body);
        if (ret == false)
        {
            // 文件读取失败，文件不存在
            body += "<html>";
            body += "<head>";
            body += "<meta charset='UTF-8'/>";
            body += "</head>";
            body += "<body>";
            body += "<h1> Not Found </h1>";
            body += "</body>";
            conn->set_status(websocketpp::http::status_code::not_found);
            conn->set_body(body);
            return;
        }
        else
        {
            conn->set_status(websocketpp::http::status_code::ok);
            conn->set_body(body);
        }
    }
    void http_response(wsserver_t::connection_ptr &conn, bool result, const std::string &reason,
                       websocketpp::http::status_code::value code)
    {
        Json::Value resp_json;
        resp_json["result"] = result;
        resp_json["reason"] = reason;
        std::string body;
        JsonUtil::serialize(resp_json, &body);
        conn->set_status(code);
        conn->set_body(body);
        conn->append_header("Content-Type", "application/json");
    }
    void reg(wsserver_t::connection_ptr &conn) // 用户注册功能请求
    {
        Json::Value req;
        // 1. 获取到请求正文
        std::string req_body = conn->get_request_body();
        // 2. 对正文进行反序列化得到用户名和密码
        bool ret = JsonUtil::unserialize(req_body, req);
        if (ret == false)
        {
            DBG_LOG("反序列化失败");
            return http_response(conn, false, "请求正文格式错误", websocketpp::http::status_code::bad_request);
        }
        // 3. 在数据库中新增
        if (req["username"].isNull() || req["password"].isNull())
        {
            DBG_LOG("输入用户名密码不完整");
            return http_response(conn, false, "请输入用户名/密码", websocketpp::http::status_code::bad_request);
        }
        ret = _ut.insert(req);
        if (ret == false)
        {
            DBG_LOG("向数据库中插入失败");
            return http_response(conn, false, "用户名已经被占用", websocketpp::http::status_code::bad_request);
        }
        // 4. 新增成功就返回200
        return http_response(conn, true, "注册成功", websocketpp::http::status_code::ok);
    }
    void login(wsserver_t::connection_ptr &conn) // 用户登录功能请求
    {
        Json::Value user;
        // 1. 获取到请求正文
        std::string req_body = conn->get_request_body();
        // 2. 对请求正文进行反序列得到用户名和输入的密码
        bool ret = JsonUtil::unserialize(req_body, user);
        if (ret == false)
        {
            DBG_LOG("反序列化失败");
            return http_response(conn, false, "请求正文格式错误", websocketpp::http::status_code::bad_request);
        }
        // 3. 搜索用户名和指定密码的序列对应的用户，如果不存在就表示用户名或密码错误，否则就登录成功
        if (user["username"].isNull() || user["password"].isNull())
        {
            DBG_LOG("输入用户名密码不完整");
            return http_response(conn, false, "请输入用户名/密码", websocketpp::http::status_code::bad_request);
        }
        ret = _ut.login(user);
        if (ret == false)
        {
            DBG_LOG("输入用户名或密码错误");
            return http_response(conn, false, "用户名或密码错误", websocketpp::http::status_code::bad_request);
        }
        // 4. 给客户端创建session
        session_ptr ssp = _sm.createSession(user["id"].asUInt64(), LOGIN);
        if (ssp.get() == nullptr)
        {
            DBG_LOG("创建会话失败");
            return http_response(conn, false, "创建会话失败", websocketpp::http::status_code::bad_request);
        }
        _sm.setExpirationTime(ssp->ssid(), SESSION_TIMEOUT);
        // 5. 构造json_resp返回（包括响应头部Set-Cookie）将sessionid返回
        std::string cookie_ssid = "SSID=" + std::to_string(ssp->ssid());
        conn->append_header("Set-Cookie", cookie_ssid);
        http_response(conn, true, "登录成功", websocketpp::http::status_code::ok);
    }
    bool get_cookie_val(const std::string &cookie_str, const std::string &key, std::string &value)
    {
        // Cookie: SSID=X; path=/;
        // 1. 以; 为间隔，对字符串进行分割出单个cookie
        std::string sep = "; ";
        std::vector<std::string> cookie_arr;
        StringUtil::spilt(cookie_str, sep, cookie_arr);
        // 2. 对单个cookie，使用=进行分割，拿到key和val
        for (auto &str : cookie_arr)
        {
            std::vector<std::string> tmp_arr;
            StringUtil::spilt(str, "=", tmp_arr);
            if (tmp_arr.size() != 2)
            {
                continue;
            }
            if (tmp_arr[0] == key)
            {
                value = tmp_arr[1];
                return true;
            }
        }
        return false;
    }
    void info(wsserver_t::connection_ptr &conn) // 用户信息获取功能请求
    {
        // 获取请求正文
        // 1. 获取请求中的Cookie，从其中获取ssid
        Json::Value err_resp;
        std::string cookie_str = conn->get_request_header("Cookie");
        if (cookie_str.empty())
        {
            // 如果没有cookie，返回错误“没有cookie信息，请重新登录”
            return http_response(conn, false, "找不到cookie信息，请重新登录", websocketpp::http::status_code::bad_request);
        }
        // 从cookie中获取ssid
        std::string ssid_str;
        bool ret = get_cookie_val(cookie_str, "SSID", ssid_str);
        if (ret == false)
        {
            // cookie中没有ssid，返回错误“没有ssid信息，请重新登录”
            return http_response(conn, false, "找不到ssid信息，请重新登录", websocketpp::http::status_code::bad_request);
        }
        // 2. 在session管理中查找对应的会话信息
        session_ptr ssp = _sm.getSessionBySsid(std::stol(ssid_str));
        if (ssp.get() == nullptr)
        {
            // 没有session，就认为“会话信息已经过期，请重新登录”
            return http_response(conn, false, "登录过期，请重新登录", websocketpp::http::status_code::bad_request);
        }
        // 3. 从数据库中获取用户信息组织并返回
        uint64_t uid = ssp->get_user();
        Json::Value user_info;
        ret = _ut.select_by_id(uid, user_info);
        if (ret == false)
        {
            // 找不到用户信息
            return http_response(conn, false, "找不到用户信息，请重新登录", websocketpp::http::status_code::bad_request);
        }
        std::string body;
        JsonUtil::serialize(user_info, &body);
        conn->set_body(body);
        conn->append_header("Content-Type", "application/json");
        conn->set_status(websocketpp::http::status_code::ok);
        // 刷新session时间
        _sm.setExpirationTime(ssp->ssid(), SESSION_TIMEOUT);
    }
    void http_callback(websocketpp::connection_hdl hdl) // 处理http请求的回调函数
    {
        wsserver_t::connection_ptr conn = _wssvr.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string method = req.get_method();
        std::string uri = req.get_uri();
        if (method == "POST" && uri == "/reg")
            return reg(conn);
        else if (method == "POST" && uri == "/login")
            return login(conn);
        else if (method == "GET" && uri == "/info")
            return info(conn);
        else
            return file_handle(conn);
    }
    void ws_resp(wsserver_t::connection_ptr &conn, Json::Value &resp)
    {
        std::string body;
        JsonUtil::serialize(resp, &body);
        conn->send(body);
    }
    session_ptr get_session_by_cookie(wsserver_t::connection_ptr &conn)
    {
        // 通过cookie获取到session_ptr(登录验证)
        Json::Value err_resp;
        std::string cookie_str = conn->get_request_header("Cookie");
        if (cookie_str.empty())
        {
            // 如果没有cookie，返回错误“没有cookie信息，请重新登录”
            err_resp["optype"] = "hall_ready";
            err_resp["result"] = false;
            err_resp["reason"] = "找不到cookie信息，请重新登录";
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        // 从cookie中获取ssid
        std::string ssid_str;
        bool ret = get_cookie_val(cookie_str, "SSID", ssid_str);
        if (ret == false)
        {
            // cookie中没有ssid，返回错误“没有ssid信息，请重新登录”
            err_resp["optype"] = "hall_ready";
            err_resp["result"] = false;
            err_resp["reason"] = "找不到ssid信息，请重新登录";
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        // 在session管理中查找对应的会话信息
        session_ptr ssp = _sm.getSessionBySsid(std::stol(ssid_str));
        if (ssp.get() == nullptr)
        {
            // 没有session，就认为“会话信息已经过期，请重新登录”
            err_resp["optype"] = "hall_ready";
            err_resp["result"] = false;
            err_resp["reason"] = "登录过期，请重新登录";
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        return ssp;
    }
    void wsopen_game_hall(wsserver_t::connection_ptr &conn)
    {
        // 游戏大厅的websocket长连接建立成功
        // 1. 登录验证
        Json::Value err_resp;
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
            return; // 登录验证失败
        // 2. 判断当前客户端是否重新登录
        if (_om.in_game_hall(ssp->get_user()) || _om.in_game_room(ssp->get_user()))
        {
            // 玩家重复登录
            err_resp["optype"] = "hall_ready";
            err_resp["result"] = false;
            err_resp["reason"] = "玩家重复登录";
            return ws_resp(conn, err_resp);
        }
        // 3. 将当前客户端以及链接加入到游戏大厅
        _om.enter_game_hall(ssp->get_user(), conn);
        // 4. 给客户端响应
        Json::Value resp_json;
        resp_json["optype"] = "hall_ready";
        resp_json["result"] = true;
        ws_resp(conn, resp_json);
        // 5. 设置session永久存在
        _sm.setExpirationTime(ssp->ssid(), SESSION_FOREVER);
    }
    void wsopen_game_room(wsserver_t::connection_ptr &conn)
    {
        Json::Value resp_json;
        // 1. 用户认证并获取当前用户的session
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
        {
            return; // 用户认证失败
        }
        // 2. 当前用户是否已经在游戏房间或游戏大厅中
        if (_om.in_game_hall(ssp->get_user()) || _om.in_game_room(ssp->get_user()))
        {
            // 玩家重复登录
            resp_json["optype"] = "room_ready";
            resp_json["result"] = false;
            resp_json["reason"] = "玩家重复登录";
            return ws_resp(conn, resp_json);
        }
        // 3. 判断当前用户是否已经创建好房间
        room_ptr rp = _rm.get_room_by_uid(ssp->get_user());
        if(rp.get() == nullptr)
        {
            // 没有找到玩家的房间信息
            resp_json["optype"] = "room_ready";
            resp_json["result"] = false;
            resp_json["reason"] = "没有找到玩家的房间信息";
            return ws_resp(conn, resp_json);
        }
        // 4. 将当前用户添加到_om中游戏房间
        _om.enter_game_room(ssp->get_user(), conn);
        // 5. 设置session永久存在
        _sm.setExpirationTime(ssp->ssid(), SESSION_FOREVER);
        // 6. 组织响应信息
        resp_json["optype"] = "room_ready";
        resp_json["result"] = true;
        resp_json["room_id"] = Json::UInt64(rp->id());
        resp_json["uid"] = Json::UInt64(ssp->get_user());
        resp_json["white_id"] = Json::UInt64(rp->get_white_user());
        resp_json["black_id"] = Json::UInt64(rp->get_black_user());

        return ws_resp(conn, resp_json);
    }
    void wsopen_callback(websocketpp::connection_hdl hdl) // websocket长连接建立成功之后的处理函数
    {
        // 由于websocket的长连接是基于页面的，当页面切换/关闭之后，原来的长连接就会关闭，所以这里需要游戏大厅的和游戏房间的两个长连接
        wsserver_t::connection_ptr conn = _wssvr.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/room")
        {
            // 建立游戏房间的长连接
            wsopen_game_room(conn);
        }
        else if (uri == "/hall")
        {
            // 建立游戏大厅的长连接
            wsopen_game_hall(conn);
        }
    }
    void wsclose_game_hall(wsserver_t::connection_ptr &conn)
    {
        // 0. 登录验证
        Json::Value err_resp;
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
            return; // 登录验证失败
        // 1. 将玩家从大厅移除
        _om.exit_game_hall(ssp->get_user());
        // 2. 将session的生命周期恢复,设置定时销毁
        _sm.setExpirationTime(ssp->ssid(), SESSION_TIMEOUT);
    }
    void wsclose_game_room(wsserver_t::connection_ptr &conn)
    {
        // std::cout << "收到房间退出消息";
        // 登录验证，获取会话信息
        Json::Value err_resp;
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
            return; // 登录验证失败
        // std::cout << "from 用户id:" << ssp->get_user() << std::endl;
        // 1. 将玩家从om中移除
        _om.exit_game_room(ssp->get_user());
        // 2. 将session的生命周期设置为定时销毁
        _sm.setExpirationTime(ssp->ssid(), SESSION_TIMEOUT);
        // 3. 将玩家从game room中移除
        _rm.remove_room_user(ssp->get_user());
    }
    void wsclose_callback(websocketpp::connection_hdl hdl) // websocket链接断开前的处理
    {
        wsserver_t::connection_ptr conn = _wssvr.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/room")
        {
            // 断开游戏房间的长连接
            wsclose_game_room(conn);
        }
        else if (uri == "/hall")
        {
            // 断开游戏大厅的长连接
            wsclose_game_hall(conn);
        }
    }
    void wsmsg_game_hall(wsserver_t::connection_ptr &conn, wsserver_t::message_ptr msg)
    {
        // 0. 登录验证
        Json::Value req_json, resp_json;
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
            return; // 登录验证失败
        // 获取请求信息
        std::string req_body = msg->get_payload();
        bool ret = JsonUtil::unserialize(req_body, req_json);
        if(ret == false)
        {
            resp_json["result"] = false;
            resp_json["reason"] = "请求信息解析失败";
            return ws_resp(conn, resp_json);
        }
        // 处理请求(开始对战匹配，停止对战匹配)
        if(!req_json["optype"].isNull() && req_json["optype"].asString() == "match_start")
        {
            // 开始对战匹配
            _mm.add(ssp->get_user());
            resp_json["optype"] = "match_start";
            resp_json["result"] = true;
            return ws_resp(conn, resp_json);
        }
        else if(!req_json["optype"].isNull() && req_json["optype"].asString() == "match_stop")
        {
            // 停止对战匹配
            _mm.del(ssp->get_user());
            resp_json["optype"] = "match_stop";
            resp_json["result"] = true;
            return ws_resp(conn, resp_json);
        }
        else
        {
            resp_json["optype"] = "unknow";
            resp_json["reason"] = "未知响应类型";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
    }
    void wsmsg_game_room(wsserver_t::connection_ptr &conn, wsserver_t::message_ptr msg)
    {
        // 1. 获取客户端session，识别客户端身份
        Json::Value resp_json;
        session_ptr ssp = get_session_by_cookie(conn);
        if(ssp.get() == nullptr)
            return; // 登录验证失败
        // 2. 获取房间信息
        room_ptr rp = _rm.get_room_by_uid(ssp->get_user());
        if(rp.get() == nullptr)
        {
            // 没有找到玩家的房间信息
            resp_json["optype"] = "unknow";
            resp_json["result"] = false;
            resp_json["reason"] = "没有找到玩家的房间信息";
            return ws_resp(conn, resp_json);
        }
        // 3. 对消息进行反序列化处理
        Json::Value req_json;
        std::string req_body = msg->get_payload();
        bool ret = JsonUtil::unserialize(req_body, req_json);
        if(ret == false)
        {
            resp_json["optype"] = "unknow";
            resp_json["result"] = false;
            resp_json["reason"] = "请求解析失败";
            return ws_resp(conn, resp_json);
        }
        // 4. 通过房间模块进行消息请求的处理
        return rp->handle_request(req_json);
    }
    void wsmsg_callback(websocketpp::connection_hdl hdl, wsserver_t::message_ptr msg)
    {
        wsserver_t::connection_ptr conn = _wssvr.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/room")
        {
            // 游戏房间的消息
            wsmsg_game_room(conn, msg);
        }
        else if (uri == "/hall")
        {
            // 游戏大厅的消息
            wsmsg_game_hall(conn, msg);
        }
    }

private:
    std::string _web_root;
    wsserver_t _wssvr;
    UserTable _ut;
    OnlineManager _om;
    RoomManager _rm;
    SessionManager _sm;
    MatchManager _mm;
};