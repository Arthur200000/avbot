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

	///��һ��ָ����url.
	// ʧ�ܽ��׳�һ��boost::system::system_error�쳣.
	// @param u ��Ҫ�򿪵�URL.
	// @begin example
	//   avhttp::http_stream h_stream(io_service);
	//   try
	//   {
	//     h_stream.open("http://www.boost.org");
	//   }
	//   catch (boost::system::error_code& e)
	//   {
	//     std::cerr << e.waht() << std::endl;
	//   }
	// @end example
	void open(const url &u)
	{

	}

	///��һ��ָ����url.
	// @param u ��Ҫ�򿪵�URL.
	// ͨ��ec���û��ִ��״̬.
	// @begin example
	//   avhttp::http_stream h_stream(io_service);
	//   boost::system::error_code ec;
	//   h_stream.open("http://www.boost.org", ec);
	//   if (ec)
	//   {
	//     std::cerr << e.waht() << std::endl;
	//   }
	// @end example
	void open(const url &u, boost::system::error_code &ec)
	{

	}

	///�첽��һ��ָ����URL.
	// @param u ��Ҫ�򿪵�URL.
	// @param handler ���������ڴ����ʱ. ������������������:
	// @begin code
	//  void handler(
	//    const boost::system::error_code& ec // ���ڷ��ز���״̬.
	//  );
	// @end code
	// @begin example
	//  void open_handler(const boost::system::error_code& ec)
	//  {
	//    if (!ec)
	//    {
	//      // �򿪳ɹ�!
	//    }
	//  }
	//  ...
	//  avhttp::http_stream h_stream(io_service);
	//  h_stream.async_open("http://www.boost.org", open_handler);
	// @end example
	// @��ע: handlerҲ����ʹ��boost.bind����һ�����Ϲ涨�ĺ�����
	// Ϊasync_open�Ĳ���handler.
	template <typename Handler>
	void async_open(const url &u, Handler handler)
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
