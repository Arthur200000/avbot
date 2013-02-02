//
// options.hpp
// ~~~~~~~~~~~
//
// Copyright (c) 2011 Jack (jack.wgm@gmail.com)
//

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#pragma once

#include <map>

namespace avhttp {

// ����httpѡ����. �����ÿ���Ӱ��http��������, ��Ҫ���ڶ���Http Header.
// ��key/value�ķ�ʽ�趨. �������ض���ѡ��:
// "request" {"GET" | "POST" | "HEAD"} default is "GET"
class option_set
{
public:

	// httpѡ��.
	typedef std::map<std::string, std::string> option_item;

public:
	option_set () {}
	~option_set () {}

public:

	// ���httpѡ��, ��key/value��ʽ���.
	void insert(const std::string &key, const std::string &val)
	{
		m_opts[key] = val;
	}

	// ɾ��httpѡ��.
	void remove(const std::string &key)
	{
		option_item::iterator f = m_opts.find(key);
		if (f != m_opts.end())
			m_opts.erase(f);
	}

	// ���httpѡ��.
	void clear()
	{
		m_opts.clear();
	}

	// ��������option_item.
	option_item& option_all()
	{
		return m_opts;
	}

protected:
	option_item m_opts;
};

}

#endif // __OPTIONS_H__
