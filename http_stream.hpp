#ifndef __HTTP_STREAM_HPP__
#define __HTTP_STREAM_HPP__

#pragma once

#include <boost/noncopyable.hpp>
#ifdef WIN32
#pragma warning(disable: 4267)	// ����VC����.
#endif // WIN32
#include <boost/asio.hpp>

#include "options.hpp"

namespace avhttp {

// һ��http����ʵ��, ����ͬ�����첽����http����.
// 
class http_stream : public boost::noncopyable
{
public:
	http_stream(boost::asio::io_service &io)
		: m_io_service(io)
	{}

	virtual ~http_stream()
	{}

public:

	// request
	// open
	// read_some
	// async_read_some


protected:
	boost::asio::io_service &m_io_service;
	options m_opts;
};

}


#endif // __HTTP_STREAM_HPP__
