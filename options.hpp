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

// ѡ���.
typedef std::map<std::string, std::string> option_item;

class option
{
public:
	option() {}
	~option() {}

public:

	// ���ѡ��, ��key/value��ʽ���.
	void insert(const std::string &key, const std::string &val)
	{
		m_opts[key] = val;
	}

	// ɾ��ѡ��.
	void remove(const std::string &key)
	{
		option_item::iterator f = m_opts.find(key);
		if (f != m_opts.end())
			m_opts.erase(f);
	}

	// ���.
	void clear()
	{
		m_opts.clear();
	}

	// ��������option.
	option_item& option_all()
	{
		return m_opts;
	}

protected:
	option_item m_opts;
};

typedef option request_opts;	// ����ʱ��httpѡ��.
typedef option response_opts;	// http���������ص�httpѡ��.

}

#endif // __OPTIONS_H__
