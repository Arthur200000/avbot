#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#pragma once

#include <map>

namespace avhttp {

class options
{
public:

	// httpѡ��.
	typedef std::map<std::string, std::string> option_item;

public:
	options () {}
	~options () {}

public:

	// ���httpѡ��.
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
