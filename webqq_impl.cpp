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
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/json_parser.hpp>
namespace js = boost::property_tree::json_parser;
#include <boost/algorithm/string.hpp>

#include <urdl/read_stream.hpp>
#include <urdl/http.hpp>

#include "webqq.h"
#include "webqq_impl.h"
#include "defer.hpp"
#include "url.hpp"
#include "logger.h"
#include "utf8/utf8.h"

extern "C"{
#include "md5.h"
};

#include <boost/foreach.hpp>

using namespace qq;

/* URL for webqq login */
#define LWQQ_URL_LOGIN_HOST "http://ptlogin2.qq.com"
#define LWQQ_URL_CHECK_HOST "http://check.ptlogin2.qq.com"
#define LWQQ_URL_VERIFY_IMG "http://captcha.qq.com/getimage?aid=%s&uin=%s"
#define VCCHECKPATH "/check"
#define APPID "1003903"
#define LWQQ_URL_SET_STATUS "http://d.web2.qq.com/channel/login2"
/* URL for get webqq version */
#define LWQQ_URL_VERSION "http://ui.ptlogin2.qq.com/cgi-bin/ver"

#define LWQQ_URL_REFERER_QUN_DETAIL "http://s.web2.qq.com/proxy.html?v=20110412001&id=3"
#define LWQQ_URL_REFERER_DISCU_DETAIL "http://d.web2.qq.com/proxy.html?v=20110331002&id=2"

#define LWQQ_URL_SEND_QUN_MSG "http://d.web2.qq.com/channel/send_qun_msg2"

static void upcase_string(char *str, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        if (islower(str[i]))
            str[i]= toupper(str[i]);
    }
}

///low level special char mapping
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
    char buf[20] = {0};
    
    srand(time(NULL));
    r = rand() % 90 + 10;

#ifdef WIN32
	sprintf(buf, "%d%d%d%d", r, r, r);
#else
	if (gettimeofday(&tv, NULL)) {
		return NULL;
	}
	t = tv.tv_usec % 1000000;
	snprintf(buf, sizeof(buf), "%d%ld", r, t);
#endif

	return buf;
}

// ptui_checkVC('0','!IJG, ptui_checkVC('0','!IJG', '\x00\x00\x00\x00\x54\xb3\x3c\x53');
static std::string parse_verify_uin(const char *str)
{
    const char *start;
    const char *end;

    start = strchr(str, '\\');
    if (!start)
        return "";

    end = strchr(start,'\'');
    if (!end)
        return "";

    return std::string(start, end - start);
}

static std::string get_cookie(std::string httpheader,std::string key)
{
	std::string searchkey = key + "=";
	std::string::size_type keyindex = httpheader.find(searchkey);
	if (keyindex == std::string::npos)
		return "";
	keyindex +=searchkey.length();
	std::string::size_type valend = httpheader.find("; ", keyindex);
	return httpheader.substr(keyindex , valend-keyindex);
}

static void update_cookies(LwqqCookies *cookies, std::string httpheader,
                           std::string key, int update_cache)
{
	std::string value = get_cookie(httpheader, key);
    if (value.empty())
        return ;
    
#define FREE_AND_STRDUP(a, b)    a = b
    
    if (key ==  "ptvfsession") {
        FREE_AND_STRDUP(cookies->ptvfsession, value);
    } else if ((key== "ptcz")) {
        FREE_AND_STRDUP(cookies->ptcz, value);
    } else if ((key== "skey")) {
        FREE_AND_STRDUP(cookies->skey, value);
    } else if ((key== "ptwebqq")) {
        FREE_AND_STRDUP(cookies->ptwebqq, value);
    } else if ((key== "ptuserinfo")) {
        FREE_AND_STRDUP(cookies->ptuserinfo, value);
    } else if ((key== "uin")) {
        FREE_AND_STRDUP(cookies->uin, value);
    } else if ((key== "ptisp")) {
        FREE_AND_STRDUP(cookies->ptisp, value);
    } else if ((key== "pt2gguin")) {
        FREE_AND_STRDUP(cookies->pt2gguin, value);
    } else if ((key== "verifysession")) {
        FREE_AND_STRDUP(cookies->verifysession, value);
    } else {
        lwqq_log(LOG_WARNING, "No this cookie: %s\n", key.c_str());
    }
#undef FREE_AND_STRDUP

    if (update_cache) {
		
		cookies->lwcookies.clear();

        if (cookies->ptvfsession.length()) {
            cookies->lwcookies += "ptvfsession="+cookies->ptvfsession+"; ";
        }
        if (cookies->ptcz.length()) {
			cookies->lwcookies += "ptcz="+cookies->ptcz+"; ";
        }
        if (cookies->skey.length()) {
            cookies->lwcookies += "skey="+cookies->skey+"; ";
        }
        if (cookies->ptwebqq.length()) {
            cookies->lwcookies += "ptwebqq="+cookies->ptwebqq+"; ";
        }
        if (cookies->ptuserinfo.length()) {
			cookies->lwcookies += "ptuserinfo="+cookies->ptuserinfo+"; ";
        }
        if (cookies->uin.length()) {
            cookies->lwcookies += "uin="+cookies->uin+"; ";
        }
        if (cookies->ptisp.length()) {
            cookies->lwcookies += "ptisp="+cookies->ptisp+"; ";
        }
        if (cookies->pt2gguin.length()) {
			cookies->lwcookies += "pt2gguin="+cookies->pt2gguin+"; ";
        }
        if (cookies->verifysession.length()) {
			cookies->lwcookies += "verifysession="+cookies->verifysession+"; ";
        }
    }
}

static void sava_cookie(LwqqCookies * cookies,std::string  httpheader)
{
	update_cookies(cookies, httpheader, "ptcz", 0);
    update_cookies(cookies, httpheader, "skey",  0);
    update_cookies(cookies, httpheader, "ptwebqq", 0);
    update_cookies(cookies, httpheader, "ptuserinfo", 0);
    update_cookies(cookies, httpheader, "uin", 0);
    update_cookies(cookies, httpheader, "ptisp", 0);
    update_cookies(cookies, httpheader, "pt2gguin", 1);
}


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
static std::string lwqq_enc_pwd(const char *pwd, const char *vc, const char *uin)
{
    int i;
    int uin_byte_length;
    char buf[128] = {0};
    char _uin[9] = {0};

    /* Calculate the length of uin (it must be 8?) */
    uin_byte_length = strlen(uin) / 4;

    /**
     * Ok, parse uin from string format.
     * "\x00\x00\x00\x00\x54\xb3\x3c\x53" -> {0,0,0,0,54,b3,3c,53}
     */
    for (i = 0; i < uin_byte_length ; i++) {
        char u[5] = {0};
        char tmp;
        strncpy(u, uin + i * 4 + 2, 2);

        errno = 0;
        tmp = strtol(u, NULL, 16);
        if (errno) {
            return NULL;
        }
        _uin[i] = tmp;
    }

    /* Equal to "var I=hexchar2bin(md5(M));" */
    lutil_md5_digest((unsigned char *)pwd, strlen(pwd), (char *)buf);

    /* Equal to "var H=md5(I+pt.uin);" */
    memcpy(buf + 16, _uin, uin_byte_length);
    lutil_md5_data((unsigned char *)buf, 16 + uin_byte_length, (char *)buf);
    
    /* Equal to var G=md5(H+C.verifycode.value.toUpperCase()); */
    sprintf(buf + strlen(buf), /*sizeof(buf) - strlen(buf),*/ "%s", vc);
    upcase_string(buf, strlen(buf));

    lutil_md5_data((unsigned char *)buf, strlen(buf), (char *)buf);
    upcase_string(buf, strlen(buf));

    /* OK, seems like every is OK */
    lwqq_puts(buf);
    return buf;
}

static pt::ptree json_parse(const char * doc)
{
	pt::ptree jstree;
	std::stringstream stream;
	stream <<  doc ;
	js::read_json(stream, jstree);
	return jstree;
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


typedef boost::function<void (const boost::system::error_code& ec, read_streamptr stream,  boost::asio::streambuf &) > urdlhandler;

static void urdl_cb_reaading(read_streamptr stream,urdlhandler handler,
	boost::shared_ptr<boost::asio::streambuf> sb, 
	const boost::system::error_code& ec, std::size_t length,
	std::size_t readed)
{
	if (!ec){
		// re read
		readed += length;
		sb->commit(length);

		stream->async_read_some(
			sb->prepare(4096),
			boost::bind(&urdl_cb_reaading, stream, handler, sb,
						boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred,
						readed)
		);
		return ;
	}
	//	real callback
	handler(ec,stream,*sb);
}

static void urdl_cb_connected(read_streamptr stream, urdlhandler handler, const boost::system::error_code& ec)
{
	boost::shared_ptr<boost::asio::streambuf> sb = boost::make_shared<boost::asio::streambuf>();
	if (!ec){
		stream->async_read_some(
			sb->prepare(4096),
			boost::bind(& urdl_cb_reaading,stream, handler, sb, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, 0)
		);
	}else{
		handler(ec, stream, *sb);
	}
}

static void urdl_download(read_streamptr stream, const urdl::url & url, urdlhandler handler)
{
	stream->async_open(url,
			boost::bind(& urdl_cb_connected, stream, handler, boost::asio::placeholders::error) 
	);
}

static void timeout(boost::shared_ptr<boost::asio::deadline_timer> t, boost::function<void()> cb)
{
	cb();
}

static void delayedcall(boost::asio::io_service &io_service, int sec, boost::function<void()> cb)
{
	boost::shared_ptr<boost::asio::deadline_timer> t( new boost::asio::deadline_timer(io_service, boost::posix_time::seconds(sec)));
	t->async_wait(boost::bind(&timeout, t, cb));
}

static void delayedcallms(boost::asio::io_service &io_service, int msec, boost::function<void()> cb)
{
	boost::shared_ptr<boost::asio::deadline_timer> t( new boost::asio::deadline_timer(io_service, boost::posix_time::milliseconds(msec)));
	t->async_wait(boost::bind(&timeout, t, cb));
}


// build webqq and setup defaults
qq::WebQQ::WebQQ(boost::asio::io_service& _io_service,
	std::string _qqnum, std::string _passwd, LWQQ_STATUS _status)
	:m_io_service(_io_service), m_qqnum(_qqnum), m_passwd(_passwd), m_status(_status)
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
#endif

	init_face_map();
}

void qq::WebQQ::start()
{
	//首先获得版本.
	read_streamptr stream(new urdl::read_stream(m_io_service));
	lwqq_log(LOG_DEBUG, "Get webqq version from %s\n", LWQQ_URL_VERSION);
	
	urdl_download(stream, LWQQ_URL_VERSION,
		boost::bind(&WebQQ::cb_got_version, this, boost::asio::placeholders::error, _2, _3)
	);
}

/**login*/
void WebQQ::login()
{
	m_clientid.clear();
	m_cookies.lwcookies.clear();
	m_groups.clear();
	m_psessionid.clear();
	m_vfwebqq.clear();
	m_status = LWQQ_STATUS_OFFLINE;
	//获取验证码.
	std::string url = 
		boost::str(boost::format("%s%s?uin=%s&appid=%s") % LWQQ_URL_CHECK_HOST % VCCHECKPATH % m_qqnum % APPID);
	std::string cookie = 
		boost::str(boost::format("chkuin=%s") % m_qqnum);
	read_streamptr stream(new urdl::read_stream(m_io_service));

	stream->set_option(urdl::http::cookie(cookie));
	urdl_download(stream, url, boost::bind(&WebQQ::cb_got_vc,this, boost::asio::placeholders::error, _2, _3));
}

void WebQQ::send_group_message(qqGroup& group, std::string msg, send_group_message_cb donecb)
{
	send_group_message(group.gid, msg, donecb);
}

void WebQQ::send_group_message(std::wstring group, std::string msg, send_group_message_cb donecb)
{
	//check if already in sending a message
	if (m_group_msg_insending){
		m_msg_queue.push(boost::make_tuple(group, msg, donecb));
		return;
	}else{
		m_group_msg_insending = true;
		send_group_message_internal(group, msg, donecb);
	}
}

void WebQQ::send_group_message_internal(std::wstring group, std::string msg, send_group_message_cb donecb)
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
		% wide_utf8(group) % parse_unescape(msg) % m_msg_id % m_clientid % m_psessionid 
		% m_clientid
		% m_psessionid
	);
	std::string postdataencoded = url_whole_encode(postdata);
	lwqq_puts(postdataencoded.c_str());

	read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::request_method("POST"));
	stream->set_option(urdl::http::cookie(this->m_cookies.lwcookies));
	stream->set_option(urdl::http::request_referer("http://d.web2.qq.com/proxy.html?v=20101025002"));
	stream->set_option(urdl::http::request_content_type("application/x-www-form-urlencoded; charset=UTF-8"));
	stream->set_option(urdl::http::request_content(postdataencoded));

	urdl_download(stream, LWQQ_URL_SEND_QUN_MSG, 
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
			delayedcall(m_io_service,10,boost::bind(&WebQQ::login,this));
		}
	}catch (const pt::json_parser_error & jserr)
	{
		lwqq_log(LOG_ERROR, "parse json error : %s\n=========\n%s\n=========\n",jserr.what(), jserr.message().c_str());
	}
	catch (const pt::ptree_bad_path & badpath){
		lwqq_log(LOG_ERROR, "bad path %s\n", badpath.what());
	}
	
	if (m_msg_queue.empty()){
		m_group_msg_insending = false;
	}else{
		boost::tuple<std::wstring, std::string, send_group_message_cb> v = m_msg_queue.front();
		delayedcallms(m_io_service, 500, boost::bind(&WebQQ::send_group_message_internal, this,boost::get<0>(v),boost::get<1>(v), boost::get<2>(v)));
		m_msg_queue.pop();
	}
	donecb(ec);
}

void WebQQ::cb_got_version ( const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer )
{
	const char* response = boost::asio::buffer_cast<const char*> ( buffer.data() );

	if ( strstr ( response, "ptuiV" ) )
	{
		char* s, *t;
		s = ( char* ) strchr ( response, '(' );
		t = ( char* ) strchr ( response, ')' );

		if ( !s || !t ) {
			sigerror ( 0, 0 );
			return ;
		}

		s++;
		char v[t - s + 1];
		memset ( v, 0, t - s + 1 );
		strncpy ( v, s, t - s );
		this->m_version = v;
		std::cout << "Get webqq version: " <<  this->m_version <<  std::endl;
		//开始真正的登录.
		login();
	}
}

void WebQQ::update_group_list()
{
	lwqq_log(LOG_NOTICE, "getting group list\n");
    /* Create post data: {"h":"hello","vfwebqq":"4354j53h45j34"} */
    std::string posdata = create_post_data(this->m_vfwebqq);
    std::string url = boost::str(boost::format("%s/api/get_group_name_list_mask2") % "http://s.web2.qq.com");

	read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::request_method("POST"));
	stream->set_option(urdl::http::cookie(this->m_cookies.lwcookies));
	stream->set_option(urdl::http::request_referer("http://s.web2.qq.com/proxy.html?v=20101025002"));
	stream->set_option(urdl::http::request_content_type("application/x-www-form-urlencoded; charset=UTF-8"));
	stream->set_option(urdl::http::request_content(posdata));

	urdl_download(stream, url,
		boost::bind(&WebQQ::cb_group_list, this, boost::asio::placeholders::error, _2, _3)
	);
}

void WebQQ::update_group_qqmember(qqGroup& group)
{
	std::string url;

	url = boost::str(
		boost::format("%s/api/get_friend_uin2?tuin=%s&verifysession=&type=1&code=&vfwebqq=%s&t=%ld")
		% "http://s.web2.qq.com"
		% wide_utf8(group.code)
		% m_vfwebqq
		% time(NULL)
	);
	read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::cookie(this->m_cookies.lwcookies));
	stream->set_option(urdl::http::request_referer(LWQQ_URL_REFERER_QUN_DETAIL));
	
	urdl_download(stream, url,
		boost::bind(&WebQQ::cb_group_qqnumber, this, boost::asio::placeholders::error, _2, _3, boost::ref(group))
	);
}

void WebQQ::update_group_member(qqGroup& group)
{
	read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::cookie(this->m_cookies.lwcookies));

	std::string url = boost::str(
		boost::format("%s/api/get_group_info_ext2?gcode=%s&vfwebqq=%s&t=%ld")
		% "http://s.web2.qq.com"
		% wide_utf8(group.code)
		% m_vfwebqq
		% time(NULL)
	);
	stream->set_option(urdl::http::request_referer(LWQQ_URL_REFERER_QUN_DETAIL));
	urdl_download(stream, url,
		boost::bind(&WebQQ::cb_group_member, this, boost::asio::placeholders::error, _2, _3, boost::ref(group))
	);
}

qqGroup* WebQQ::get_Group_by_gid(std::wstring gid)
{
	qq::grouplist::iterator it = m_groups.find(gid);
	if (it != m_groups.end())
		return & it->second;
	return NULL;
}

qqGroup* WebQQ::get_Group_by_qq(std::wstring qq)
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

	read_streamptr loginstream(new urdl::read_stream(m_io_service));
	loginstream->set_option(urdl::http::cookie(m_cookies.lwcookies));
	loginstream->async_open(url,boost::bind(&WebQQ::cb_do_login,this, loginstream, boost::asio::placeholders::error) );
}

void WebQQ::cb_got_vc(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer)
{
	/**
	* 
	* The http message body has two format:
	*
	* ptui_checkVC('1','9ed32e3f644d968809e8cbeaaf2cce42de62dfee12c14b74');
	* ptui_checkVC('0','!LOB');
	* The former means we need verify code image and the second
	* parameter is vc_type.
	* The later means we don't need the verify code image. The second
	* parameter is the verify code. The vc_type is in the header
	* "Set-Cookie".
	*/
	const char * response = boost::asio::buffer_cast<const char * >(buffer.data());
	char *s;
	char *c = (char*)strstr(response, "ptui_checkVC");
    c = (char*)strchr(response, '\'');
    c++;
    if (*c == '0') {
        /* We got the verify code. */
        
        /* Parse uin first */
        m_verifycode.uin = parse_verify_uin(response);
        if (m_verifycode.uin.empty())
		{
			sigerror(1, 0);
			return;
		}

        s = c;
        c = strstr(s, "'");
        s = c + 1;
        c = strstr(s, "'");
        s = c + 1;
        c = strstr(s, "'");
        *c = '\0';

        /* We need get the ptvfsession from the header "Set-Cookie" */
        update_cookies(&m_cookies, stream->headers(), "ptvfsession", 1);
        lwqq_log(LOG_NOTICE, "Verify code: %s\n", s);

        login_withvc(s);
    } else if (*c == '1') {
        /* We need get the verify image. */

        /* Parse uin first */
        m_verifycode.uin = parse_verify_uin(response);
        s = c;
        c = strstr(s, "'");
        s = c + 1;
        c = strstr(s, "'");
        s = c + 1;
        c = strstr(s, "'");
        *c = '\0';

        // ptui_checkVC('1','7ea19f6d3d2794eb4184c9ae860babf3b9c61441520c6df0', '\x00\x00\x00\x00\x04\x7e\x73\xb2');

        lwqq_log(LOG_NOTICE, "We need verify code image: %s\n", s);

        //TODO, get verify image, and call signeedvc
        get_verify_image(s);
    }
}

void WebQQ::get_verify_image(std::string vcimgid)
{
	std::string url = boost::str(
		boost::format(LWQQ_URL_VERIFY_IMG) % APPID % m_qqnum
	);

	read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::cookie(std::string("chkuin=") + m_qqnum));
	urdl_download(stream, url,
		boost::bind(&WebQQ::cb_get_verify_image,this, boost::asio::placeholders::error, _2, _3));
}

void WebQQ::cb_get_verify_image(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer)
{
	update_cookies(&m_cookies, stream->headers() , "verifysession", 1);
	
	// verify image is now in response
	signeedvc(buffer.data());
}

void WebQQ::cb_done_login(read_streamptr stream, char* response, const boost::system::error_code& ec, std::size_t length)
{
	defer(boost::bind(operator delete, response));
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
        sava_cookie(&m_cookies, stream->headers());
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

    read_streamptr stream(new urdl::read_stream(m_io_service));
	stream->set_option(urdl::http::cookie(m_cookies.lwcookies));
	stream->set_option(urdl::http::request_content_type("application/x-www-form-urlencoded; charset=UTF-8"));
	stream->set_option(urdl::http::request_referer("http://d.web2.qq.com/proxy.html?v=20110331002&callback=1&id=2"));
	stream->set_option(urdl::http::request_content(msg));
	stream->set_option(urdl::http::request_method("POST"));
	stream->set_option(urdl::http::user_agent("Mozilla/5.0 (X11; Linux x86_64; rv:17.0) Gecko/20100101 Firefox/17.0"));

	stream->async_open(LWQQ_URL_SET_STATUS,boost::bind(&WebQQ::cb_online_status,this, stream, boost::asio::placeholders::error) );
}

void WebQQ::do_poll_one_msg()
{
    /* Create a POST request */
	std::string msg = boost::str(
		boost::format("{\"clientid\":\"%s\",\"psessionid\":\"%s\"}")
		% m_clientid 
		% m_psessionid		
	);

	msg = boost::str(boost::format("r=%s") %  url_encode(msg.c_str()));

    read_streamptr pollstream(new urdl::read_stream(m_io_service));
	pollstream->set_option(urdl::http::cookie(m_cookies.lwcookies));
	pollstream->set_option(urdl::http::cookie2("$Version=1"));
	pollstream->set_option(urdl::http::request_content_type("application/x-www-form-urlencoded; charset=UTF-8"));
	pollstream->set_option(urdl::http::request_referer("http://d.web2.qq.com/proxy.html?v=20101025002"));
	pollstream->set_option(urdl::http::request_content(msg));
	pollstream->set_option(urdl::http::request_method("POST"));

	pollstream->async_open("http://d.web2.qq.com/channel/poll2",
		boost::bind(&WebQQ::cb_poll_msg,this, pollstream, boost::asio::placeholders::error) );
}

void WebQQ::cb_online_status(read_streamptr stream, char* response, const boost::system::error_code& ec, std::size_t length)
{
	defer(boost::bind(operator delete, response));
	//处理!
	try{
		pt::ptree json = json_parse(response);
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
		printf("parse json error : %s \n\t %s\n", jserr.what(), response);
	}catch (const pt::ptree_bad_path & jserr){
		printf("parse bad path error :  %s\n", jserr.what());
	}
}

void WebQQ::cb_poll_msg(read_streamptr stream, char* response, const boost::system::error_code& ec, std::size_t length, size_t goten)
{
	if (!ec)
	{
		goten += length;
		stream->async_read_some(
			boost::asio::buffer(response + goten, 16384 - goten),
			boost::bind(&WebQQ::cb_poll_msg, this, stream, response, boost::asio::placeholders::error,boost::asio::placeholders::bytes_transferred, goten));
		return ;
	}

	defer(boost::bind(operator delete, response));

	//开启新的 poll
	if ( m_status == LWQQ_STATUS_ONLINE )
		do_poll_one_msg();

	if (ec != boost::asio::error::eof){
		return;
	}

	std::wstringstream jsondata;
	jsondata << std::string(response).c_str();
	pt::wptree	jsonobj;

	//处理!
	try{
		pt::json_parser::read_json(jsondata, jsonobj);
		process_msg(jsonobj);
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n=========\n%s\n=========\n",jserr.what(), response);
	}
	catch (const pt::ptree_bad_path & badpath){
		lwqq_log(LOG_ERROR, "bad path %s\n", badpath.what());
		js::write_json(std::wcout, jsonobj);
	}
}

void WebQQ::process_group_message ( const boost::property_tree::wptree& jstree )
{
	std::wstring group_code = jstree.get<std::wstring> ( L"value.from_uin" );
	std::wstring who = jstree.get<std::wstring> ( L"value.send_uin" );

	//parse content
	std::vector<qqMsg>	messagecontent;

	BOOST_FOREACH ( const pt::wptree::value_type & content,jstree.get_child ( L"value.content" ) )
	{
		if ( content.second.count ( L"" ) ) {
			if ( content.second.begin()->second.data() == L"font" ) {
				qqMsg msg;
				msg.type = qqMsg::LWQQ_MSG_FONT;
				msg.font = content.second.rbegin()->second.get<std::wstring> ( L"name" );
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
				msg.cface = content.second.rbegin()->second.get<std::wstring> ( L"name" );
				messagecontent.push_back ( msg );
			}
		} else {
			//聊天字符串就在这里.
			qqMsg msg;
			msg.type = qqMsg::LWQQ_MSG_TEXT;
			msg.text = content.second.data();
			messagecontent.push_back ( msg );
		}
	}
	siggroupmessage ( group_code, who, messagecontent );
}

void WebQQ::process_msg(const pt::wptree &jstree)
{
	//在这里解析json数据.
	int retcode = jstree.get<int>(L"retcode");
	if (retcode)
		return;

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
			delayedcall(m_io_service, 15, boost::bind(&WebQQ::login, this));
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
 				newgroup.gid = utf8_wide(result.second.get<std::string>("gid"));
 				newgroup.name = utf8_wide(result.second.get<std::string>("name"));
 				newgroup.code = utf8_wide(result.second.get<std::string>("code"));
 				if (newgroup.gid[0]==L'-'){
					retry = true;
					lwqq_log(LOG_ERROR, "qqGroup get error \n");
					continue;
				}

 				this->m_groups.insert(std::make_pair(newgroup.gid, newgroup));
 				lwqq_log(LOG_DEBUG, "qq群 %ls %ls\n",newgroup.gid.c_str(), newgroup.name.c_str());
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
		delayedcall(m_io_service, 5, boost::bind(&WebQQ::update_group_list, this));
	}else{
		// fetching more budy info.
		BOOST_FOREACH(grouplist::value_type & v, m_groups)
		{
			update_group_qqmember(v.second);
			update_group_member(v.second);
		}
		//start polling messages, 2 connections!
		lwqq_log(LOG_DEBUG, "start polling messages\n");
		do_poll_one_msg();
		do_poll_one_msg();
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
			group.qqnum = utf8_wide(jsonobj.get<std::string>("result.account"));
			lwqq_log(LOG_NOTICE, "qq number of group %ls is %ls\n", group.name.c_str(), group.qqnum.c_str());
		}
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n", jserr.what());

		delayedcall(m_io_service, 5, boost::bind(&WebQQ::update_group_member, this, boost::ref(group)));
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
			BOOST_FOREACH(pt::ptree::value_type & v, jsonobj.get_child("result.minfo"))
			{
				qqBuddy buddy;
				pt::ptree & minfo = v.second;
				buddy.nick = utf8_wide(minfo.get<std::string>("nick"));
				buddy.uin = utf8_wide(minfo.get<std::string>("uin"));

				group.memberlist.insert(std::make_pair(buddy.uin, buddy));
				lwqq_log(LOG_DEBUG, "buddy list:: %ls %ls\n", buddy.uin.c_str(), buddy.nick.c_str());
			}
			BOOST_FOREACH(pt::ptree::value_type & v, jsonobj.get_child("result.cards"))
			{
				pt::ptree & minfo = v.second;
				std::wstring muin = utf8_wide(minfo.get<std::string>("muin"));
				std::wstring card = utf8_wide(minfo.get<std::string>("card"));
				group.get_Buddy_by_uin(muin)->card = card;
			}
		}
	}catch (const pt::json_parser_error & jserr){
		lwqq_log(LOG_ERROR, "parse json error : %s\n", jserr.what());

		delayedcall(m_io_service, 5, boost::bind(&WebQQ::update_group_member, this, boost::ref(group)));
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

void WebQQ::cb_online_status(read_streamptr stream, const boost::system::error_code& ec)
{
	char * data = new char[8192];
	memset(data, 0, 8192);
	boost::asio::async_read(*stream, boost::asio::buffer(data, 8192),
		boost::bind(&WebQQ::cb_online_status, this, stream, data, boost::asio::placeholders::error,  boost::asio::placeholders::bytes_transferred) );
}

void WebQQ::cb_poll_msg(read_streamptr stream, const boost::system::error_code& ec)
{
	char * data = new char[16384];
	memset(data, 0, 16384);
	boost::asio::async_read(*stream, boost::asio::buffer(data, 16384),
		boost::bind(&WebQQ::cb_poll_msg, this, stream, data, boost::asio::placeholders::error,  boost::asio::placeholders::bytes_transferred, 0) );
}

std::string WebQQ::lwqq_status_to_str(LWQQ_STATUS status)
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
