#include <utility>
#include "http_server.h"

void HttpServer::Init(const std::string &port)
{
	m_port = port;
	m_listen_on = m_url+":"+port;
	//s_server_option.enable_directory_listing = "yes";
	s_server_option.root_dir = s_web_dir.c_str();

	// 其他http设置

	// 开启 CORS，本项只针对主页加载有效
	// s_server_option.extra_headers = "Access-Control-Allow-Origin: *";
}

bool HttpServer::Start()
{
	mg_mgr_init(&m_mgr);
	mg_connection *connection = mg_http_listen(&m_mgr, m_listen_on.c_str(), HttpServer::OnHttpWebsocketEvent, nullptr);
	if (connection == NULL)
		return false;
	// for both http and websocket
	//mg_set_protocol_http_websocket(connection);

	printf("starting http server at port: %s\n", m_port.c_str());
	// loop
	while (true)
		mg_mgr_poll(&m_mgr, 500); // ms

	return true;
}

void HttpServer::OnHttpWebsocketEvent(mg_connection *connection, int event_type, void *event_data, void *fn_data)
{
	// 区分http和websocket
	if (event_type == MG_EV_HTTP_MSG)
	{
		mg_http_message *http_req = (mg_http_message *)event_data;
		HandleHttpEvent(connection, http_req);
	}
	else if (event_type == MG_EV_WS_MSG ||
		     event_type == MG_EV_WS_CTL ||
			 event_type == MG_EV_WS_OPEN ||
		     event_type == MG_EV_CLOSE)
	{
		if(connection->is_websocket != 1){
			mg_ws_upgrade(connection, (mg_http_message *)event_data, nullptr);
		}else{
			mg_ws_message *ws_message = (mg_ws_message *)event_data;
			HandleWebsocketMessage(connection, event_type, ws_message);
		}
	}
}

// ---- simple http ---- //
static bool route_check(mg_http_message *http_msg, char *route_prefix)
{
	if (mg_vcmp(&http_msg->uri, route_prefix) == 0)
		return true;
	else
		return false;

	// TODO: 还可以判断 GET, POST, PUT, DELTE等方法
	//mg_vcmp(&http_msg->method, "GET");
	//mg_vcmp(&http_msg->method, "POST");
	//mg_vcmp(&http_msg->method, "PUT");
	//mg_vcmp(&http_msg->method, "DELETE");
}

void HttpServer::AddHandler(const std::string &url, ReqHandler req_handler)
{
	if (s_handler_map.find(url) != s_handler_map.end())
		return;

	s_handler_map.insert(std::make_pair(url, req_handler));
}

void HttpServer::RemoveHandler(const std::string &url)
{
	auto it = s_handler_map.find(url);
	if (it != s_handler_map.end())
		s_handler_map.erase(it);
}

void HttpServer::SendHttpRsp(mg_connection *connection, std::string rsp)
{
	// --- 未开启CORS
	// 必须先发送header, 暂时还不能用HTTP/2.0
	mg_printf(connection, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");
	// 以json形式返回
	mg_printf_http_chunk(connection, "{ \"result\": %s }", rsp.c_str());
	// 发送空白字符快，结束当前响应
	mg_send_http_chunk(connection, "", 0);

	// --- 开启CORS
	/*mg_printf(connection, "HTTP/1.1 200 OK\r\n"
			  "Content-Type: text/plain\n"
			  "Cache-Control: no-cache\n"
			  "Content-Length: %d\n"
			  "Access-Control-Allow-Origin: *\n\n"
			  "%s\n", rsp.length(), rsp.c_str()); */
}

void HttpServer::HandleHttpEvent(mg_connection *connection, mg_http_message *http_req)
{
	std::string req_str = std::string(http_req->message.p, http_req->message.len);
	printf("got request: %s\n", req_str.c_str());

	// 先过滤是否已注册的函数回调
	std::string url = std::string(http_req->uri.p, http_req->uri.len);
	std::string body = std::string(http_req->body.p, http_req->body.len);
	auto it = s_handler_map.find(url);
	if (it != s_handler_map.end())
	{
		ReqHandler handle_func = it->second;
		handle_func(url, body, connection, &HttpServer::SendHttpRsp);
	}

	// 其他请求
	if (route_check(http_req, "/")) // index page
		mg_serve_http(connection, http_req, s_server_option);
	else if (route_check(http_req, "/api/hello")) 
	{
		// 直接回传
		SendHttpRsp(connection, "welcome to httpserver");
	}
	else if (route_check(http_req, "/api/sum"))
	{
		// 简单post请求，加法运算测试
		char n1[100], n2[100];
		double result;

		/* Get form variables */
		mg_get_http_var(&http_req->body, "n1", n1, sizeof(n1));
		mg_get_http_var(&http_req->body, "n2", n2, sizeof(n2));

		/* Compute the result and send it back as a JSON object */
		result = strtod(n1, NULL) + strtod(n2, NULL);
		SendHttpRsp(connection, std::to_string(result));
	}
	else
	{
		mg_printf(
			connection,
			"%s",
			"HTTP/1.1 501 Not Implemented\r\n" 
			"Content-Length: 0\r\n\r\n");
	}
}

// ---- websocket ---- //
// int HttpServer::isWebsocket(const mg_connection *connection)
// {
// 	return connection->flags & MG_F_IS_WEBSOCKET;
// }

void HttpServer::HandleWebsocketMessage(mg_connection *connection, int event_type, mg_ws_message *ws_msg)
{
	if (event_type == MG_EV_WS_OPEN)
	{
		printf("client websocket connected\n");
		// 获取连接客户端的IP和端口
		//char addr[32];
		//mg_sock_addr_to_str(&connection->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
		if(connection->peer.ip && connection->peer.port){
			BYTE ip_addr[4], ip_port[2];
			memcpy(ip_addr, (const void*)(connection->peer.ip+3), 1);
			memcpy(ip_port+1, (const void*)(connection->peer.port+2), 1);
			memcpy(ip_port+2, (const void*)(connection->peer.port+1), 1);
			memcpy(ip_port+3, (const void*)(connection->peer.port), 1);
			memcpy(ip_port, (const void*)(connection->peer.port+1), 1);
			memcpy(ip_port+1, (const void*)(connection->peer.port), 1);
			printf("client addr: %d.%d.%d.%d:%d\n", (INT8)ip_addr[0],(INT8)ip_addr[1],(INT8)ip_addr[2],(INT8)ip_addr[4],(INT16)ip_port);
		}
		

		// 添加 session
		s_websocket_session_set.insert(connection);

		SendWebsocketMsg(connection, "client websocket connected");
	}
	else if (event_type == MG_EV_WS_MSG)
	{
		mg_str received_msg = {
			(char *)ws_msg->data.ptr, ws_msg->data.len;
		};

		char buff[1024] = {0};
		strncpy(buff, received_msg.p, received_msg.len); // must use strncpy, specifiy memory pointer and length

		// do sth to process request
		printf("received msg: %s\n", buff);
		SendWebsocketMsg(connection, "send your msg back: " + std::string(buff));
		//BroadcastWebsocketMsg("broadcast msg: " + std::string(buff));
	}
	else if (event_type == MG_EV_CLOSE)
	{
		if (connection->is_websocket)
		{
			printf("client websocket closed\n");
			// 移除session
			if (s_websocket_session_set.find(connection) != s_websocket_session_set.end())
				s_websocket_session_set.erase(connection);
		}
	}
}

void HttpServer::SendWebsocketMsg(mg_connection *connection, std::string msg)
{
	mg_ws_send(connection, msg.c_str(), strlen(msg.c_str()), WEBSOCKET_OP_TEXT);
}

void HttpServer::BroadcastWebsocketMsg(std::string msg)
{
	for (mg_connection *connection : s_websocket_session_set)
		mg_ws_send(connection, msg.c_str(), strlen(msg.c_str()),WEBSOCKET_OP_TEXT);
}

bool HttpServer::Close()
{
	mg_mgr_free(&m_mgr);
	return true;
}