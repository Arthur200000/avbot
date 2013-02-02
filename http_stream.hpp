//
// http_stream.hpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2011 Jack (jack.wgm@gmail.com)
//

#ifndef __HTTP_STREAM_HPP__
#define __HTTP_STREAM_HPP__

#pragma once

#include "detail/abi_prefix.hpp"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/noncopyable.hpp>

#include "url.hpp"
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

	void open(const url& u) // throw
	{

	}

	// request
	// open
	// async_open
	// read_some
	// async_read_some


protected:
	boost::asio::io_service &m_io_service;
	request_opts m_req_opts;						// ��http�����������ͷ��Ϣ.
	response_opts m_resp_opts;						// http���������ص�httpͷ��Ϣ.
};

}

#include "detail/abi_suffix.hpp"

#endif // __HTTP_STREAM_HPP__
