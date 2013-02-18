//
// file.hpp
// ~~~~~~~~
//
// Copyright (c) 2013 Jack (jack dot wgm at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// path LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef __FILE_HPP__
#define __FILE_HPP__

#include <boost/noncopyable.hpp>
#include "storage_interface.hpp"

namespace avhttp
{

class file
	: public storage_interface
	, public boost::noncopyable
{
public:
	file() {}
	virtual ~file() { close(); }

public:

	// �洢�����ʼ��.
	// @param file_pathָ�����ļ���·����Ϣ.
	// @param ec�ڳ���ʱ��������ϸ�Ĵ�����Ϣ.
	virtual void open(fs::path &file_path, boost::system::error_code &ec)
	{

	}

	// �رմ洢���.
	virtual void close()
	{

	}

	// д������.
	// @param buf����Ҫд������ݻ���.
	// @param offset��д���ƫ��λ��.
	// @param sizeָ����д������ݻ����С.
	// @����ֵΪʵ��д����ֽ���, ����-1��ʾд��ʧ��.
	virtual int write(const char *buf, boost::uint64_t offset, int size)
	{
		return -1;
	}

	// ��ȡ����.
	// @param buf����Ҫ��ȡ�����ݻ���.
	// @param offset�Ƕ�ȡ��ƫ��λ��.
	// @param sizeָ���˶�ȡ�����ݻ����С.
	// @����ֵΪʵ�ʶ�ȡ���ֽ���, ����-1��ʾ��ȡʧ��.
	virtual int read(char *buf, boost::uint64_t offset, int size)
	{
		return -1;
	}

protected:

};

// Ĭ�ϴ洢����.
storage_interface* default_storage_constructor()
{
	return new file();
}

}

#endif // __FILE_HPP__
