//
// settings.hpp
// ~~~~~~~~~~~~
//
// Copyright (c) 2013 Jack (jack dot wgm at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// path LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef __SETTINGS_HPP__
#define __SETTINGS_HPP__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <vector>
#include <map>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>

#include "storage_interface.hpp"

namespace avhttp {

// ���û�ж�������ض������, ��Ĭ��Ϊ5������ض���.
#ifndef AVHTTP_MAX_REDIRECTS
#define AVHTTP_MAX_REDIRECTS 5
#endif

namespace http_options {

	// ����һЩ���õġ�http ѡ��Ϊ const string , �����Ͳ��ü�����Щ�����ˣ��Ǻ�.
	static const std::string request_method("_request_method");
	static const std::string request_body("_request_body");
	static const std::string status_code("_status_code");
	static const std::string cookie("cookie");
	static const std::string referer("Referer");
	static const std::string content_type("Content-Type");
	static const std::string content_length("Content-Length");
	static const std::string connection("Connection");
} // namespace http_options

// �����http��optionѡ��ʵ��.

class option
{
public:
	// ����option_item����.
	typedef std::pair<std::string, std::string> option_item;
	// ����option_item_list����.
	typedef std::vector<option_item> option_item_list;
	// for boost::assign::insert
	typedef option_item value_type;
public:
	option() {}
	~option() {}

public:

	// ����������������Ӧ��:
	// http_stream s;
	// s.request_options(request_opts()("cookie","XXXXXX"));
	option & operator()(const std::string &key, const std::string &val)
	{
		insert(key, val);
		return *this;
	}

	// ���ѡ��, ��key/value��ʽ���.
	void insert(const std::string &key, const std::string &val)
	{
		m_opts.push_back(option_item(key, val));
	}

	// ���ѡ��� std::part ��ʽ.
	void insert(value_type & item)
	{
		m_opts.push_back(item);
	}

	// ɾ��ѡ��.
	void remove(const std::string &key)
	{
		for (option_item_list::iterator i = m_opts.begin(); i != m_opts.end(); i++)
		{
			if (i->first == key)
			{
				m_opts.erase(i);
				return;
			}
		}
	}

	// ����ָ��key��value.
	bool find(const std::string &key, std::string &val) const
	{
		std::string s = key;
		boost::to_lower(s);
		for (option_item_list::const_iterator f = m_opts.begin(); f != m_opts.end(); f++)
		{
			std::string temp = f->first;
			boost::to_lower(temp);
			if (temp == s)
			{
				val = f->second;
				return true;
			}
		}
		return false;
	}

	// ����ָ���� key �� value. û�ҵ����� ""�������Ǹ�͵���İ���.
	std::string find(const std::string & key) const
	{
		std::string v;
		find(key,v);
		return v;
	}

	// �õ�Header�ַ���.
	std::string header_string() const
	{
		std::string str;
		for (option_item_list::const_iterator f = m_opts.begin(); f != m_opts.end(); f++)
		{
			if (f->first != http_options::status_code)
				str += (f->first + ": " + f->second + "\r\n");
		}
		return str;
	}

	// ���.
	void clear()
	{
		m_opts.clear();
	}

	// ��������option.
	option_item_list& option_all()
	{
		return m_opts;
	}

protected:
	option_item_list m_opts;
};

// ����ʱ��httpѡ��.
// ����ѡ��Ϊ��httpѡ��:
// _request_method, ȡֵ "GET/POST/HEAD", Ĭ��Ϊ"GET".
// _request_body, �����е�body����, ȡֵ����, Ĭ��Ϊ��.
// Host, ȡֵΪhttp������, Ĭ��Ϊhttp������.
// Accept, ȡֵ����, Ĭ��Ϊ"*/*".
typedef option request_opts;

// http���������ص�httpѡ��.
// һ���������¼���ѡ��:
// _status_code, http����״̬.
// Server, ����������.
// Content-Length, �������ݳ���.
// Connection, ����״̬��ʶ.
typedef option response_opts;



// Http����Ĵ�������.

struct proxy_settings
{
	std::string hostname;
	int port;

	std::string username;
	std::string password;

	enum proxy_type
	{
		// û�����ô���.
		none,
		// socks4����, ��Ҫusername.
		socks4,
		// ����Ҫ�û������socks5����.
		socks5,
		// ��Ҫ�û�������֤��socks5����.
		socks5_pw,
		// http����, ����Ҫ��֤.
		http,
		// http����, ��Ҫ��֤.
		http_pw,
	};

	proxy_type type;

	// when set to true, hostname are resolved
	// through the proxy (if supported)
	bool proxy_hostnames;

	// if true, use this proxy for peers too
	bool proxy_peer_connections;
};


// һЩĬ�ϵ�ֵ.
static const int default_request_piece_num = 10;
static const int default_time_out = 11;
static const int default_piece_size = 32768;
static const int default_connections_limit = 5;
static const int default_buffer_size = 1024;

// multi_download��������.

struct settings
{
	// ����ģʽ.
	enum downlad_mode
	{
		// ����ģʽ����, ����ģʽ��ָ, ���ļ���Ƭ��, ���ļ�ͷ��ʼ, һƬ������һƬ,
		// �������ϵ�����.
		compact_mode,

		// TODO: ��ɢģʽ����, ��ָ���ļ���Ƭ��, ��������ƽ��ΪN����������.
		dispersion_mode,

		// TODO: ���ٶ�ȡģʽ����, ���ģʽ�Ǹ����û���ȡ����λ�ÿ�ʼ��������, �Ǿ�����Ӧ
		// �����û���Ҫ������.
		quick_read_mode
	};

	settings ()
		: download_rate_limit(-1)
		, connections_limit(default_connections_limit)
		, piece_size(default_piece_size)
		, time_out(default_time_out)
		, request_piece_num(default_request_piece_num)
		, current_downlad_mode(dispersion_mode)
		, storage(NULL)
	{}

	// ������������, -1Ϊ������, ��λΪ: byte/s.
	int download_rate_limit;

	// ����������, -1ΪĬ��.
	int connections_limit;

	// �ֿ��С, Ĭ�ϸ����ļ���С�Զ�����.
	int piece_size;

	// ��ʱ�Ͽ�, Ĭ��Ϊ11��.
	int time_out;

	// ÿ������ķ�Ƭ��, Ĭ��Ϊ10.
	int request_piece_num;

	// ����ģʽ, Ĭ��Ϊdispersion_mode.
	downlad_mode current_downlad_mode;

	// meta_file·��, Ĭ��Ϊ��ǰ·����ͬ�ļ�����.meta�ļ�.
	fs::path m_meta_file;

	// �洢�ӿڴ�������ָ��, Ĭ��Ϊmulti_download�ṩ��file.hppʵ��.
	storage_constructor_type storage;
};

} // namespace avhttp

#endif // __SETTINGS_HPP__
