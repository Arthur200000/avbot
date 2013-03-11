/*
 * Copyright (C) 2012 - 2013  微蔡 <microcai@fedoraproject.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <string.h>
#include <iostream>
#include <boost/system/system_error.hpp>
#include <boost/bind.hpp>
#include <boost/bind/protect.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>
namespace js = boost::property_tree::json_parser;
#include <boost/algorithm/string.hpp>
#include <boost/foreach.hpp>
#include <boost/assign.hpp>
#include <boost/scope_exit.hpp>

#include "boost/timedcall.hpp"

#include "constant.hpp"
#include "webqq.h"
#include "webqq_impl.h"
#include "md5.hpp"
#include "url.hpp"
#include "logger.h"
#include "utf8.hpp"
#include "webqq_login.hpp"

using namespace qq;

static std::string generate_clientid();

static void upcase_string(char *str, int len);
///low level special char mapping
static std::string parse_unescape(std::string source);

static std::string lwqq_status_to_str(LWQQ_STATUS status);

/**
 * I hacked the javascript file named comm.js, which received from tencent
 * server, and find that fuck tencent has changed encryption algorithm
 * for password in webqq3 . The new algorithm is below(descripted with javascript):
 * var M=C.p.value; // M is the qq password 
 * var I=hexchar2bin(md5(M)); // Make a md5 digest
 * var H=md5(I+pt.uin); // Make md5 with I and uin(see below)
 * var G=md5(H+C.verifycode.value.toUpperCase());
 * 
 * @param pwd User's password
 * @param vc Verify Code. e.g. "!M6C"
 * @param uin A string like "\x00\x00\x00\x00\x54\xb3\x3c\x53", NB: it
 *        must contain 8 hexadecimal number, in this example, it equaled
 *        to "0x0,0x0,0x0,0x0,0x54,0xb3,0x3c,0x53"
 * 
 * @return Encoded password on success, else NULL on failed
 */
static std::string lwqq_enc_pwd(const std::string & pwd, const std::string & vc, const std::string &uin)
{
    int i;
    int uin_byte_length;
    char buf[128] = {0};
    char _uin[9] = {0};

    /* Calculate the length of uin (it must be 8?) */
    uin_byte_length = uin.length() / 4;

    /**
     * Ok, parse uin from string format.
     * "\x00\x00\x00\x00\x54\xb3\x3c\x53" -> {0,0,0,0,54,b3,3c,53}
     */
    for (i = 0; i < uin_byte_length ; i++) {
        char u[5] = {0};
        char tmp;
        strncpy(u, & uin [  i * 4 + 2 ] , 2);

        errno = 0;
        tmp = strtol(u, NULL, 16);
        if (errno) {
            return NULL;
        }
        _uin[i] = tmp;
    }

    /* Equal to "var I=hexchar2bin(md5(M));" */
    lutil_md5_digest((unsigned char *)pwd.c_str(), pwd.length(), (char *)buf);

    /* Equal to "var H=md5(I+pt.uin);" */
    memcpy(buf + 16, _uin, uin_byte_length);
    lutil_md5_data((unsigned char *)buf, 16 + uin_byte_length, (char *)buf);
    
    /* Equal to var G=md5(H+C.verifycode.value.toUpperCase()); */
    std::sprintf(buf + strlen(buf), /*sizeof(buf) - strlen(buf),*/ "%s", vc.c_str());
    upcase_string(buf, strlen(buf));

    lutil_md5_data((unsigned char *)buf, strlen(buf), (char *)buf);
    upcase_string(buf, strlen(buf));

    /* OK, seems like every is OK */
    lwqq_puts(buf);
    return buf;
}

static std::string create_post_data(std::string vfwebqq)
{
    std::string m = boost::str(boost::format("{\"h\":\"hello\",\"vfwebqq\":\"%s\"}") % vfwebqq);
    return std::string("r=") + url_encode(m.c_str());
}

static pt::wptree json_parse(const wchar_t * doc)
{
	pt::wptree jstree;
	std::wstringstream stream;
	stream <<  doc ;
	js::read_json(stream, jstree);
	return jstree;
}

namespace qq {
namespace detail {

// qq 登录办法-验证码登录
class corologin_vc : boost::coro::coroutine {
public:
	corologin_vc(WebQQ & webqq )
		:m_webqq(webqq)
	{

		
	}

	// 在这里实现　QQ 的登录.
	void operator()(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf & buf)
	{
		//　登录步骤.
		reenter(this)
		{

		}
	}
private:
	WebQQ & m_webqq;
};


}
}

// build webqq and setup defaults
WebQQ::WebQQ(boost::asio::io_service& _io_service,
	std::string _qqnum, std::string _passwd, LWQQ_STATUS _status)
	:m_io_service(_io_service), m_qqnum(_qqnum), m_passwd(_passwd), m_status(_status),
	m_msg_queue(20) //　最多保留最后的20条未发送消息.
{
#ifndef _WIN32
	/* Set msg_id */
    timeval tv;
    long v;
    gettimeofday(&tv, NULL);
    v = tv.tv_usec;
    v = (v - v % 1000) / 1000;
    v = v % 10000 * 10000;
    m_msg_id = v;
#else
	m_msg_id = std::rand();
#endif

	init_face_map();
}

/**login*/
void WebQQ::login()
{
	qq::detail::corologin(*this);
}

void WebQQ::send_group_message(qqGroup& group, std::string msg, send_group_message_cb donecb)
{
	send_group_message(group.gid, msg, donecb);
}

void WebQQ::send_group_message(std::string group, std::string msg, send_group_message_cb donecb)
{
	//check if already in sending a message
	if (m_group_msg_insending){
		m_msg_queue.push_back(boost::make_tuple(group, msg, donecb));
		return;
	}else{
		m_group_msg_insending = true;
		send_group_message_internal(group, msg, donecb);
	}
}

void WebQQ::send_group_message_internal(std::string group, std::string msg, send_group_message_cb donecb)
{
	//unescape for POST
	std::string postdata = boost::str(
		boost::format("r={\"group_uin\":\"%s\", "
					"\"content\":\"["
					"\\\"%s\\\","
						"[\\\"font\\\",{\\\"name\\\":\\\"宋体\\\",\\\"size\\\":\\\"9\\\",\\\"style\\\":[0,0,0],\\\"color\\\":\\\"000000\\\"}]"
					  "]\","
				"\"msg_id\":%ld,"
				"\"clientid\":\"%s\","
				"\"psessionid\":\"%s\"}&clientid=%s&psessionid=%s")
		% group % parse_unescape(msg) % m_msg_id % m_clientid % m_psessionid 
		% m_clientid
		% m_psessionid
	);

	read_streamptr stream(new avhttp::http_stream(m_io_service));
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::request_method, "POST")
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::referer, "http://d.web2.qq.com/proxy.html?v=20101025002")
			(avhttp::http_options::content_type, "application/x-www-form-urlencoded; charset=UTF-8")
			(avhttp::http_options::request_body, postdata)
			(avhttp::http_options::content_length, boost::lexical_cast<std::string>(postdata.length()))
			(avhttp::http_options::connection, "close")
	);

	async_http_download(stream, LWQQ_URL_SEND_QUN_MSG, 
		boost::bind(&WebQQ::cb_send_msg, this, boost::asio::placeholders::error, _2, _3, donecb)
	);
}

void WebQQ::cb_send_msg(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf & buffer, boost::function<void (const boost::system::error_code& ec)> donecb)
{
	pt::ptree jstree;
	std::istream	response(&buffer);
	try{
		js::read_json(response, jstree);
		if( jstree.get<int>("retcode") == 108){
			// 已经断线，重新登录
			m_status = LWQQ_STATUS_UNKNOW;
			// 10s 后登录.
			boost::delayedcallsec(m_io_service,10,boost::bind(&WebQQ::login,this));
		}
	}catch (const pt::json_parser_error & jserr)
	{
		std::istream	response(&buffer);
		lwqq_log(LOG_ERROR, "parse json error : %s\n=========\n%s\n=========\n",jserr.what(), jserr.message().c_str());
		std::cerr << response;
	}
	catch (const pt::ptree_bad_path & badpath){
		lwqq_log(LOG_ERROR, "bad path %s\n", badpath.what());
	}
	
	if (m_msg_queue.empty()){
		m_group_msg_insending = false;
	}else{
		boost::tuple<std::string, std::string, send_group_message_cb> v = m_msg_queue.front();
		boost::delayedcallms(m_io_service, 500, boost::bind(&WebQQ::send_group_message_internal, this,boost::get<0>(v),boost::get<1>(v), boost::get<2>(v)));
		m_msg_queue.pop_front();
	}
	m_io_service.post( boost::asio::detail::bind_handler(donecb,ec));
}

void WebQQ::update_group_list()
{
	lwqq_log(LOG_NOTICE, "getting group list\n");
    /* Create post data: {"h":"hello","vfwebqq":"4354j53h45j34"} */
    std::string postdata = create_post_data(this->m_vfwebqq);
    std::string url = boost::str(boost::format("%s/api/get_group_name_list_mask2") % "http://s.web2.qq.com");

	read_streamptr stream(new avhttp::http_stream(m_io_service));
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::request_method, "POST")
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::referer, "http://s.web2.qq.com/proxy.html?v=20101025002")
			(avhttp::http_options::content_type, "application/x-www-form-urlencoded; charset=UTF-8")
			(avhttp::http_options::request_body, postdata)
			(avhttp::http_options::content_length, boost::lexical_cast<std::string>(postdata.length()))
			(avhttp::http_options::connection, "close")
	);

	async_http_download(stream, url,
		boost::bind(&WebQQ::cb_group_list, this, boost::asio::placeholders::error, _2, _3)
	);
}

void WebQQ::update_group_qqmember(qqGroup& group)
{
	std::string url;

	url = boost::str(
		boost::format("%s/api/get_friend_uin2?tuin=%s&verifysession=&type=1&code=&vfwebqq=%s&t=%ld")
		% "http://s.web2.qq.com"
		% group.code
		% m_vfwebqq
		% time(NULL)
	);
	read_streamptr stream(new avhttp::http_stream(m_io_service));
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::referer, LWQQ_URL_REFERER_QUN_DETAIL)
			(avhttp::http_options::connection, "close")
	);

	async_http_download(stream, url,
		boost::bind(&WebQQ::cb_group_qqnumber, this, boost::asio::placeholders::error, _2, _3, boost::ref(group))
	);
}

void WebQQ::update_group_member(qqGroup& group)
{
	read_streamptr stream(new avhttp::http_stream(m_io_service));

	std::string url = boost::str(
		boost::format("%s/api/get_group_info_ext2?gcode=%s&vfwebqq=%s&t=%ld")
		% "http://s.web2.qq.com"
		% group.code
		% m_vfwebqq
		% time(NULL)
	);
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::referer, LWQQ_URL_REFERER_QUN_DETAIL)
			(avhttp::http_options::connection, "close")
	);

	async_http_download(stream, url,
		boost::bind(&WebQQ::cb_group_member, this, boost::asio::placeholders::error, _2, _3, boost::ref(group))
	);
}

qqGroup* WebQQ::get_Group_by_gid(std::string gid)
{
	qq::grouplist::iterator it = m_groups.find(gid);
	if (it != m_groups.end())
		return & it->second;
	return NULL;
}

qqGroup* WebQQ::get_Group_by_qq(std::string qq)
{
	qq::grouplist::iterator it = m_groups.begin();
	for(;it != m_groups.end();it ++){
		if ( it->second.qqnum == qq)
			return & it->second;
	}
	return NULL;
}

// login to server with vc. called by login code or by user
// if no verify image needed, then called by login
// if verify image needed, then the user should listen to signeedvc and call this
void WebQQ::login_withvc(std::string vccode)
{
	std::cout << "vc code is \"" << vccode << "\"" << std::endl;
	std::string md5 = lwqq_enc_pwd(m_passwd.c_str(), vccode.c_str(), m_verifycode.uin.c_str());

    // do login !
    std::string url = boost::str(
		boost::format(
		    "%s/login?u=%s&p=%s&verifycode=%s&"
             "webqq_type=%d&remember_uin=1&aid=1003903&login2qq=1&"
             "u1=http%%3A%%2F%%2Fweb.qq.com%%2Floginproxy.html"
             "%%3Flogin2qq%%3D1%%26webqq_type%%3D10&h=1&ptredirect=0&"
             "ptlang=2052&from_ui=1&pttype=1&dumy=&fp=loginerroralert&"
             "action=2-11-7438&mibao_css=m_webqq&t=1&g=1") 
             % LWQQ_URL_LOGIN_HOST
             % m_qqnum
             % md5
             % vccode
             % m_status
	);

	read_streamptr loginstream(new avhttp::http_stream(m_io_service));
	loginstream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::connection, "close")
	);
	loginstream->async_open(url,boost::bind(&WebQQ::cb_do_login,this, loginstream, boost::asio::placeholders::error) );
}

void WebQQ::get_verify_image(std::string vcimgid)
{
	if( vcimgid.length() < 8){
		boost::delayedcallsec(m_io_service, 10,boost::bind(&WebQQ::login, this));
		return ;
	}

	std::string url = boost::str(
		boost::format(LWQQ_URL_VERIFY_IMG) % APPID % m_qqnum
	);

	read_streamptr stream(new avhttp::http_stream(m_io_service));
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::cookie, std::string("chkuin=") + m_qqnum)
			(avhttp::http_options::connection, "close")
	);
	async_http_download(stream, url,
		boost::bind(&WebQQ::cb_get_verify_image,this, boost::asio::placeholders::error, _2, _3));
}

void WebQQ::cb_get_verify_image(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer)
{
	qq::detail::update_cookies(&m_cookies, stream->response_options().header_string() , "verifysession", 1);
	
	// verify image is now in response
	signeedvc(buffer.data());
}

void WebQQ::cb_done_login(read_streamptr stream, char* response, const boost::system::error_code& ec, std::size_t length)
{
    BOOST_SCOPE_EXIT( (&response) )
    {
		delete response;
    } BOOST_SCOPE_EXIT_END
	
	std::cout << response << std::endl;
    char *p = strstr(response, "\'");
    if (!p) {
        return;
    }
    char buf[4] = {0};
    int status;
    strncpy(buf, p + 1, 1);
    status = atoi(buf);

    switch (status) {
    case 0:
		m_status = LWQQ_STATUS_ONLINE;
        qq::detail::save_cookie(&m_cookies, stream->response_options().header_string());
        lwqq_log(LOG_NOTICE, "login success!\n");
        break;
        
    case 1:
        lwqq_log(LOG_WARNING, "Server busy! Please try again\n");

        status = LWQQ_STATUS_OFFLINE;
		break;
	case 2:
        lwqq_log(LOG_ERROR, "Out of date QQ number\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    case 3:
        lwqq_log(LOG_ERROR, "Wrong password\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    case 4:
        lwqq_log(LOG_ERROR, "Wrong verify code\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    case 5:
        lwqq_log(LOG_ERROR, "Verify failed\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    case 6:
        lwqq_log(LOG_WARNING, "You may need to try login again\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		
    case 7:
        lwqq_log(LOG_ERROR, "Wrong input\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    case 8:
        lwqq_log(LOG_ERROR, "Too many logins on this IP. Please try again\n");
        status = LWQQ_STATUS_OFFLINE;
		break;
		

    default:
        status = LWQQ_STATUS_OFFLINE;
        lwqq_log(LOG_ERROR, "Unknow error");
    }

    //set_online_status(lc, lwqq_status_to_str(lc->stat), err);
    if (m_status == LWQQ_STATUS_ONLINE && status==0){
		siglogin();
		m_clientid = generate_clientid();

		//change status,  this is the last step for login
		set_online_status();
		m_group_msg_insending =false;
	}
}

void WebQQ::set_online_status()
{
	std::string msg = boost::str(
		boost::format("{\"status\":\"%s\",\"ptwebqq\":\"%s\","
             "\"passwd_sig\":""\"\",\"clientid\":\"%s\""
             ", \"psessionid\":null}")
		% lwqq_status_to_str(LWQQ_STATUS_ONLINE)
		% m_cookies.ptwebqq
		% m_clientid
	);

	std::string buf = url_encode(msg.c_str());
	msg = boost::str(boost::format("r=%s") % buf);

    read_streamptr stream(new avhttp::http_stream(m_io_service));
	stream->request_options(
		avhttp::request_opts()
			(avhttp::http_options::request_method, "POST")
			(avhttp::http_options::cookie, m_cookies.lwcookies)
			(avhttp::http_options::referer, "http://d.web2.qq.com/proxy.html?v=20110331002&callback=1&id=2")
			(avhttp::http_options::content_type, "application/x-www-form-urlencoded; charset=UTF-8")
			(avhttp::http_options::request_body, msg)
			(avhttp::http_options::content_length, boost::lexical_cast<std::string>(msg.length()))
			(avhttp::http_options::connection, "close")
	);

	async_http_download(stream, LWQQ_URL_SET_STATUS , 
		boost::bind(&WebQQ::cb_online_status,this, _1,_2,_3)
	);
}

void WebQQ::do_poll_one_msg(std::string cookie)
{
    /* Create a POST request */
	std::string msg = boost::str(
		boost::format("{\"clientid\":\"%s\",\"psessionid\":\"%s\"}")
		% m_clientid 
		% m_psessionid		
	);

	msg = boost::str(boost::format("r=%s") %  url_encode(msg.c_str()));

    read_streamptr pollstream(new avhttp::http_stream(m_io_service));
	pollstream->request_options(avhttp::request_opts()
		(avhttp::http_options::request_method, "POST")
		(avhttp::http_options::cookie, cookie)
		("cookie2", "$Version=1")
		(avhttp::http_options::referer, "http://d.web2.qq.com/proxy.html?v=20101025002")
		(avhttp::http_options::request_body, msg)
		(avhttp::http_options::content_type, "application/x-www-form-urlencoded; charset=UTF-8")
		(avhttp::http_options::content_length, boost::lexical_cast<std::string>(msg.length()))
 		(avhttp::http_options::connection, "close")
	);

	async_http_download(pollstream, "http://d.web2.qq.com/channel/poll2",
		boost::bind(&WebQQ::cb_poll_msg,this, _1, _2, _3, cookie)
	);
}

void WebQQ::cb_poll_msg(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buf, std::string cookie)
{
	if(cookie != m_cookies.lwcookies)
	{
		return ;
	}

	//开启新的 poll
	do_poll_one_msg(cookie);

	std::wstring response = utf8_wide(std::string(boost::asio::buffer_cast<const char*>(buf.data()) , buf.size()));
	
	pt::wptree	jsonobj;
	
	std::wstringstream jsondata;
	jsondata << response;

	//处理!
	try{
 		pt::json_parser::read_json(jsondata, jsonobj);
		process_msg(jsonobj);
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n",jserr.what());
	}
	catch (const pt::ptree_bad_path & badpath){
		lwqq_log(LOG_ERROR, "bad path %s\n", badpath.what());
		js::write_json(std::wcout, jsonobj);
	}
}

void WebQQ::cb_online_status(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buf)
{
	std::istream response(&buf);
    //处理!
	try{
		pt::ptree json;

		js::read_json(response, json);
		js::write_json(std::cout, json);

		if ( json.get<std::string>("retcode") == "0")
		{
			m_psessionid = json.get_child("result").get<std::string>("psessionid");
			m_vfwebqq = json.get_child("result").get<std::string>("vfwebqq");
			m_status = LWQQ_STATUS_ONLINE;
			//polling group list
			update_group_list();
		}
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR , "parse json error : %s \n", jserr.what());
	}catch (const pt::ptree_bad_path & jserr){
		lwqq_log(LOG_ERROR , "parse bad path error :  %s\n", jserr.what());
	}
}

void WebQQ::process_group_message ( const boost::property_tree::wptree& jstree )
{
	std::string group_code = wide_utf8( jstree.get<std::wstring>(L"value.from_uin") );
	std::string who = wide_utf8( jstree.get<std::wstring>(L"value.send_uin") );

	//parse content
	std::vector<qqMsg>	messagecontent;

	BOOST_FOREACH ( const pt::wptree::value_type & content,jstree.get_child ( L"value.content" ) )
	{
		if ( content.second.count ( L"" ) ) {
			if ( content.second.begin()->second.data() == L"font" ) {
				qqMsg msg;
				msg.type = qqMsg::LWQQ_MSG_FONT;
				msg.font = wide_utf8(content.second.rbegin()->second.get<std::wstring> ( L"name" ));
				messagecontent.push_back ( msg );
			} else if ( content.second.begin()->second.data() == L"face" ) {
				qqMsg msg;
				msg.type = qqMsg::LWQQ_MSG_FACE;
				int wface = boost::lexical_cast<int>(content.second.rbegin()->second.data());
				msg.face = facemap[wface];
 				messagecontent.push_back ( msg );
			} else if ( content.second.begin()->second.data() == L"cface" ) {
				qqMsg msg;
				msg.type = qqMsg::LWQQ_MSG_CFACE;
				msg.cface = wide_utf8(content.second.rbegin()->second.get<std::wstring> ( L"name" ));
				messagecontent.push_back ( msg );
			}
		} else {
			//聊天字符串就在这里.
			qqMsg msg;
			msg.type = qqMsg::LWQQ_MSG_TEXT;
			msg.text = wide_utf8(content.second.data());
			messagecontent.push_back ( msg );
		}
	}
	siggroupmessage ( group_code, who, messagecontent );
}

void WebQQ::process_msg(const pt::wptree &jstree)
{
	//在这里解析json数据.
	int retcode = jstree.get<int>(L"retcode");
	if (retcode){
		if(retcode != 102){
			boost::delayedcallsec(m_io_service, 15, boost::bind(&WebQQ::login, this));
		}
		return;
	}

	BOOST_FOREACH(const pt::wptree::value_type & result, jstree.get_child(L"result"))
	{
		std::string poll_type = wide_utf8(result.second.get<std::wstring>(L"poll_type"));
		if (poll_type == "group_message")
		{
			process_group_message(result.second);
		}else if (poll_type == "sys_g_msg")
		{
			//群消息.
			if (result.second.get<std::wstring>(L"value.type") == L"group_join")
			{
				//新加列表，reload群列表.
 				update_group_list();
			}
		}else if (poll_type == "buddylist_change")
		{
			//群列表变化了，reload列表.
			js::write_json(std::wcout, result.second);			
		}else if (poll_type == "kick_message"){
			js::write_json(std::wcout, result.second);
			m_status = LWQQ_STATUS_OFFLINE;
			//强制下线了，重登录.
			boost::delayedcallsec(m_io_service, 15, boost::bind(&WebQQ::login, this));
		}
	}
}

void WebQQ::cb_group_list(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer)
{
	pt::ptree	jsonobj;
  	std::istream jsondata(&buffer);
	bool retry = false;

	//处理!
	try{
		pt::json_parser::read_json(jsondata, jsonobj);
		
		//TODO, group list
		if (!(retry = !(jsonobj.get<int>("retcode") == 0)))
		{
			BOOST_FOREACH(pt::ptree::value_type result, 
							jsonobj.get_child("result").get_child("gnamelist"))
			{
				qqGroup	newgroup;
 				newgroup.gid = result.second.get<std::string>("gid");
 				newgroup.name = result.second.get<std::string>("name");
 				newgroup.code = result.second.get<std::string>("code");
 				if (newgroup.gid[0]=='-'){
					retry = true;
					lwqq_log(LOG_ERROR, "qqGroup get error \n");
					continue;
				}

 				this->m_groups.insert(std::make_pair(newgroup.gid, newgroup));
 				lwqq_log(LOG_DEBUG, "qq群 %s %s\n",newgroup.gid.c_str(), newgroup.name.c_str());
			}
		}
	}catch (const pt::json_parser_error & jserr){
		retry = true;
		lwqq_log(LOG_ERROR, "parse json error : %s \n", jserr.what());
	}catch (const pt::ptree_bad_path & badpath){
		retry = true;
	 	lwqq_log(LOG_ERROR, "bad path error %s\n", badpath.what());
	}
	if (retry){
		boost::delayedcallsec(m_io_service, 5, boost::bind(&WebQQ::update_group_list, this));
	}else{
		// fetching more budy info.
		BOOST_FOREACH(grouplist::value_type & v, m_groups)
		{
			update_group_qqmember(v.second);
			update_group_member(v.second);
		}
		//start polling messages, 2 connections!
		lwqq_log(LOG_DEBUG, "start polling messages\n");
		do_poll_one_msg(m_cookies.lwcookies);
		do_poll_one_msg(m_cookies.lwcookies);
	}
}

void WebQQ::cb_group_qqnumber(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer, qqGroup & group)
{
	pt::ptree	jsonobj;
	std::istream jsondata(&buffer);

    /**
     * Here, we got a json object like this:
     * {"retcode":0,"result":{"uiuin":"","account":615050000,"uin":954663841}}
     *
     */
	//处理!
	try{
		pt::json_parser::read_json(jsondata, jsonobj);
		js::write_json(std::cout, jsonobj);

		//TODO, group members
		if (jsonobj.get<int>("retcode") == 0)
		{
			group.qqnum = jsonobj.get<std::string>("result.account");
			lwqq_log(LOG_NOTICE, "qq number of group %s is %s\n", group.name.c_str(), group.qqnum.c_str());
		}
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n", jserr.what());

		boost::delayedcallsec(m_io_service, 5, boost::bind(&WebQQ::update_group_member, this, boost::ref(group)));
	}catch (const pt::ptree_bad_path & badpath){
	 	lwqq_log(LOG_ERROR, "bad path error %s\n", badpath.what());
	}
}

void WebQQ::cb_group_member(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer, qqGroup& group)
{
	pt::ptree	jsonobj;
	std::istream jsondata(&buffer);

	//处理!
	try{
		pt::json_parser::read_json(jsondata, jsonobj);

		//TODO, group members
		if (jsonobj.get<int>("retcode") == 0)
		{
			group.owner = jsonobj.get<std::string>("result.ginfo.owner");

			BOOST_FOREACH(pt::ptree::value_type & v, jsonobj.get_child("result.minfo"))
			{
				qqBuddy buddy;
				pt::ptree & minfo = v.second;
				buddy.nick = minfo.get<std::string>("nick");
				buddy.uin = minfo.get<std::string>("uin");

				group.memberlist.insert(std::make_pair(buddy.uin, buddy));
				lwqq_log(LOG_DEBUG, "buddy list:: %s %s\n", buddy.uin.c_str(), buddy.nick.c_str());
			}
			BOOST_FOREACH(pt::ptree::value_type & v, jsonobj.get_child("result.ginfo.members"))
			{
				pt::ptree & minfo = v.second;
				std::string muin = minfo.get<std::string>("muin");
				std::string mflag = minfo.get<std::string>("mflag");
				try{
				group.get_Buddy_by_uin(muin)->mflag = boost::lexical_cast<unsigned int>(mflag);
				}catch(boost::bad_lexical_cast & e){}
			}
			BOOST_FOREACH(pt::ptree::value_type & v, jsonobj.get_child("result.cards"))
			{
				pt::ptree & minfo = v.second;
				std::string muin = minfo.get<std::string>("muin");
				std::string card = minfo.get<std::string>("card");
				group.get_Buddy_by_uin(muin)->card = card;
			}
		}
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n", jserr.what());

		boost::delayedcallsec(m_io_service, 5, boost::bind(&WebQQ::update_group_member, this, boost::ref(group)));
	}catch (const pt::ptree_bad_path & badpath){
	 	lwqq_log(LOG_ERROR, "bad path error %s\n", badpath.what());
	}
}


void WebQQ::cb_do_login(read_streamptr stream, const boost::system::error_code& ec)
{
	char * data = new char[8192];
	boost::asio::async_read(*stream, boost::asio::buffer(data, 8192),
		boost::bind(&WebQQ::cb_done_login, this,stream, data, boost::asio::placeholders::error,  boost::asio::placeholders::bytes_transferred) );
}

static std::string lwqq_status_to_str(LWQQ_STATUS status)
{
    switch(status){
        case LWQQ_STATUS_ONLINE: return "online";break;
        case LWQQ_STATUS_OFFLINE: return "offline";break;
        case LWQQ_STATUS_AWAY: return "away";break;
        case LWQQ_STATUS_HIDDEN: return "hidden";break;
        case LWQQ_STATUS_BUSY: return "busy";break;
        case LWQQ_STATUS_CALLME: return "callme";break;
        case LWQQ_STATUS_SLIENT: return "slient";break;
        default: return "unknow";break;
    }
}

static void upcase_string(char *str, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        if (islower(str[i]))
            str[i]= toupper(str[i]);
    }
}

static std::string parse_unescape(std::string source)
{
	boost::replace_all(source, "\\", "\\\\\\\\");
	boost::replace_all(source, "\n", "\\\\n");
	boost::replace_all(source, "\t", "\\\\t");
	boost::replace_all(source, ":", "\\\\u003A");
	boost::replace_all(source, ";", "\\\\u003B");
	boost::replace_all(source, "&", "\\\\u0026");
	boost::replace_all(source, "+", "\\\\u002B");
	boost::replace_all(source, "%", "\\\\u0025");
	boost::replace_all(source, "`", "\\\\u0060");
	boost::replace_all(source, "[`", "\\\\u005B");
	boost::replace_all(source, "]", "\\\\u005D");
	boost::replace_all(source, ",", "\\\\u002C");
	boost::replace_all(source, "{", "\\\\u007B");
	boost::replace_all(source, "}", "\\\\u007D");
	return source;
}

static std::string generate_clientid()
{
    int r;
    struct timeval tv;
    long t;

    srand(time(NULL));
    r = rand() % 90 + 10;

#ifdef WIN32
	return boost::str(boost::format("%d%d%d") % r % r %r );
#else
	if (gettimeofday(&tv, NULL)) {
		return NULL;
	}
	t = tv.tv_usec % 1000000;
	return boost::str(boost::format("%d%ld") % r % t);
#endif
}