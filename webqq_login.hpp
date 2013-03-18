
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

#pragma once
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/json_parser.hpp>
namespace js = boost::property_tree::json_parser;

#include <avhttp.hpp>

#include "boost/timedcall.hpp"
#include "boost/coro/coro.hpp"
#include "boost/coro/yield.hpp"

#include "httpagent.hpp"

#include "webqq_impl.h"

#include "constant.hpp"
#include "md5.hpp"
#include "url.hpp"

namespace qq {
namespace detail {

static void upcase_string(char *str, int len)
{
    int i;
    for (i = 0; i < len; ++i) {
        if (islower(str[i]))
            str[i]= toupper(str[i]);
    }
}

static std::string parse_version(boost::asio::streambuf& buffer)
{
	const char* response = boost::asio::buffer_cast<const char*> ( buffer.data() );

	if ( strstr ( response, "ptuiV" ) )
	{
		char* s, *t;
		s = ( char* ) strchr ( response, '(' );
		t = ( char* ) strchr ( response, ')' );

		if ( !s || !t ) {
			return "";
		}

		s++;
		char v[t - s + 1];
		memset ( v, 0, t - s + 1 );
		strncpy ( v, s, t - s );
		
		std::cout << "Get webqq version: " <<  v <<  std::endl;
		return v;
	}
	return "";
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
    return buf;
}

static std::string get_cookie(const std::string & cookie, std::string key)
{
 	std::string searchkey = key + "=";
 	std::string::size_type keyindex = cookie.find(searchkey);
 	if (keyindex == std::string::npos)
 		return "";
 	keyindex +=searchkey.length();
 	std::string::size_type valend = cookie.find("; ", keyindex);
 	return cookie.substr(keyindex , valend-keyindex);
}

static void update_cookies(LwqqCookies *cookies, const std::string & httpheader,
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

static void save_cookie(LwqqCookies * cookies, const std::string & httpheader)
{
	update_cookies(cookies, httpheader, "ptcz", 0);
    update_cookies(cookies, httpheader, "skey",  0);
    update_cookies(cookies, httpheader, "ptwebqq", 0);
    update_cookies(cookies, httpheader, "ptuserinfo", 0);
    update_cookies(cookies, httpheader, "uin", 0);
    update_cookies(cookies, httpheader, "ptisp", 0);
    update_cookies(cookies, httpheader, "pt2gguin", 1);
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

// qq 登录办法
class SYMBOL_HIDDEN corologin : boost::coro::coroutine {
public:
	corologin(qq::WebQQ & webqq )
		:m_webqq(webqq)
	{
		read_streamptr stream;//(new avhttp::http_stream(m_webqq.get_ioservice()));
		boost::asio::streambuf buf;
		(*this)(boost::system::error_code(), stream, buf);
	}

	// 在这里实现　QQ 的登录.
	void operator()(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf & buf)
	{
		//　登录步骤.
		reenter(this)
		{
			stream.reset(new avhttp::http_stream(m_webqq.get_ioservice()));
			lwqq_log(LOG_DEBUG, "Get webqq version from %s\n", LWQQ_URL_VERSION);
			// 首先获得版本.
			_yield async_http_download(stream, LWQQ_URL_VERSION, *this);

			m_webqq.m_version = parse_version(buf);
			
			// 接着获得验证码.
			stream.reset(new avhttp::http_stream(m_webqq.get_ioservice()));
			
			m_webqq.m_clientid.clear();
			m_webqq.m_cookies.clear();
			m_webqq.m_groups.clear();
			m_webqq.m_psessionid.clear();
			m_webqq.m_vfwebqq.clear();
			m_webqq.m_status = LWQQ_STATUS_OFFLINE;
			//获取验证码.

			stream->request_options(
				avhttp::request_opts()
					(avhttp::http_options::cookie, boost::str(boost::format("chkuin=%s") % m_webqq.m_qqnum))
					(avhttp::http_options::connection, "close")
			);

			_yield async_http_download(stream,
				/*url*/ boost::str(boost::format("%s%s?uin=%s&appid=%s") % LWQQ_URL_CHECK_HOST % VCCHECKPATH % m_webqq.m_qqnum % APPID),
									*this);

			// 解析验证码，然后带验证码登录.
			parse_verify_code(ec, stream , buf);
		}
	}

	void parse_verify_code(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf& buffer)
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
			m_webqq.m_verifycode.uin = parse_verify_uin(response);
			if (m_webqq.m_verifycode.uin.empty())
			{
				m_webqq.sigerror(1, 0);
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
			update_cookies(&(m_webqq.m_cookies), stream->response_options().header_string(), "ptvfsession", 1);
			lwqq_log(LOG_NOTICE, "Verify code: %s\n", s);

			m_webqq.login_withvc(s);
		} else if (*c == '1') {
			/* We need get the verify image. */

			/* Parse uin first */
			m_webqq.m_verifycode.uin = parse_verify_uin(response);
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
			m_webqq.get_verify_image(s);
		}
	}
private:
	qq::WebQQ & m_webqq;
};

// qq 登录办法-验证码登录
class SYMBOL_HIDDEN corologin_vc : boost::coro::coroutine {
public:
	corologin_vc(WebQQ & webqq, std::string _vccode )
		:m_webqq(webqq), vccode(_vccode)
	{
		std::string md5 = lwqq_enc_pwd(m_webqq.m_passwd.c_str(), vccode.c_str(), m_webqq.m_verifycode.uin.c_str());

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
				% m_webqq.m_qqnum
				% md5
				% vccode
				% m_webqq.m_status
		);

		read_streamptr stream(new avhttp::http_stream(m_webqq.get_ioservice()));
		stream->request_options(
			avhttp::request_opts()
				(avhttp::http_options::cookie, m_webqq.m_cookies.lwcookies)
				(avhttp::http_options::connection, "close")
		);
		async_http_download(stream, url , *this);
	}

	// 在这里实现　QQ 的登录.
	void operator()(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf & buffer)
	{
		std::string msg;
		pt::ptree json;
		std::istream response(&buffer);
		//　登录步骤.
		reenter(this)
		{
			//set_online_status(lc, lwqq_status_to_str(lc->stat), err);
			if( (check_login(ec, stream, buffer) == 0) && (m_webqq.m_status == LWQQ_STATUS_ONLINE) )
			{
				m_webqq.siglogin();
				m_webqq.m_clientid = generate_clientid();

				//change status,  this is the last step for login
				// 设定在线状态.
				
				msg = boost::str(
					boost::format("{\"status\":\"%s\",\"ptwebqq\":\"%s\","
						"\"passwd_sig\":""\"\",\"clientid\":\"%s\""
						", \"psessionid\":null}")
					% lwqq_status_to_str(LWQQ_STATUS_ONLINE)
					% m_webqq.m_cookies.ptwebqq
					% m_webqq.m_clientid
				);

				msg = boost::str(boost::format("r=%s") % url_encode(msg.c_str()));

				stream.reset(new avhttp::http_stream(m_webqq.get_ioservice()));
				stream->request_options(
					avhttp::request_opts()
						(avhttp::http_options::request_method, "POST")
						(avhttp::http_options::cookie, m_webqq.m_cookies.lwcookies)
						(avhttp::http_options::referer, "http://d.web2.qq.com/proxy.html?v=20110331002&callback=1&id=2")
						(avhttp::http_options::content_type, "application/x-www-form-urlencoded; charset=UTF-8")
						(avhttp::http_options::request_body, msg)
						(avhttp::http_options::content_length, boost::lexical_cast<std::string>(msg.length()))
						(avhttp::http_options::connection, "close")
				);

				_yield async_http_download(stream, LWQQ_URL_SET_STATUS , * this);
					
				//处理!
				try{
					js::read_json(response, json);
					js::write_json(std::cout, json);

					if ( json.get<std::string>("retcode") == "0")
					{
						m_webqq.m_psessionid = json.get_child("result").get<std::string>("psessionid");
						m_webqq.m_vfwebqq = json.get_child("result").get<std::string>("vfwebqq");
						m_webqq.m_status = LWQQ_STATUS_ONLINE;
						//polling group list
						m_webqq.update_group_list();
					}
				}catch (const pt::json_parser_error & jserr){
					lwqq_log(LOG_ERROR , "parse json error : %s \n", jserr.what());
				}catch (const pt::ptree_bad_path & jserr){
					lwqq_log(LOG_ERROR , "parse bad path error :  %s\n", jserr.what());
				}

				m_webqq.m_group_msg_insending = !m_webqq.m_msg_queue.empty();

				if(m_webqq.m_group_msg_insending)
				{
					boost::tuple<std::string, std::string, WebQQ::send_group_message_cb> v = m_webqq.m_msg_queue.front();
					boost::delayedcallms(m_webqq.get_ioservice(), 500, boost::bind(&WebQQ::send_group_message_internal, &m_webqq,boost::get<0>(v),boost::get<1>(v), boost::get<2>(v)));
					m_webqq.m_msg_queue.pop_front();
				}

			}else
			{
				m_webqq.get_ioservice().post(boost::bind(&WebQQ::login, &m_webqq));
			}
		}
	}

private:
	int check_login(const boost::system::error_code& ec, read_streamptr stream, boost::asio::streambuf & buffer)
	{
		const char * response = boost::asio::buffer_cast<const char*>(buffer.data());

		std::cout << response << std::endl;
		char *p = strstr((char*)response, "\'");
		if (!p) {
			return -1;
		}
		char buf[4] = {0};

		strncpy(buf, p + 1, 1);
		int status = atoi(buf);

		switch (status) {
		case 0:
			m_webqq.m_status = LWQQ_STATUS_ONLINE;
			save_cookie(&(m_webqq.m_cookies), stream->response_options().header_string());
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
		return status;
	}

private:
	WebQQ & m_webqq;
	std::string vccode;
};

}
}
