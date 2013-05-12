//
// http_stream.ipp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2013 Jack (jack dot wgm at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef __HTTP_STREAM_IPP__
#define __HTTP_STREAM_IPP__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "avhttp/http_stream.hpp"

namespace avhttp {

http_stream::http_stream(boost::asio::io_service &io)
	: m_io_service(io)
	, m_resolver(io)
	, m_sock(io)
	, m_nossl_socket(io)
	, m_check_certificate(true)
	, m_keep_alive(false)
	, m_status_code(-1)
	, m_redirects(0)
	, m_max_redirects(AVHTTP_MAX_REDIRECTS)
	, m_content_length(0)
#ifdef AVHTTP_ENABLE_ZLIB
	, m_is_gzip(false)
#endif
	, m_is_chunked(false)
	, m_skip_crlf(true)
	, m_chunked_size(0)
{
#ifdef AVHTTP_ENABLE_ZLIB
	memset(&m_stream, 0, sizeof(z_stream));
#endif
	m_proxy.type = proxy_settings::none;
}

http_stream::~http_stream()
{
#ifdef AVHTTP_ENABLE_ZLIB
	if (m_stream.zalloc)
		inflateEnd(&m_stream);
#endif
}

void http_stream::open(const url &u)
{
	boost::system::error_code ec;
	open(u, ec);
	if (ec)
	{
		boost::throw_exception(boost::system::system_error(ec));
	}
}

void http_stream::open(const url &u, boost::system::error_code &ec)
{
	const std::string protocol = u.protocol();

	// ����url.
	m_url = u;

	// ���һЩѡ��.
	m_content_type = "";
	m_status_code = 0;
	m_content_length = 0;
	m_content_type = "";
	m_request.consume(m_request.size());
	m_response.consume(m_response.size());
	m_protocol = "";
	m_skip_crlf = true;

	// ��������url����.
	if (protocol == "http")
	{
		m_protocol = "http";
	}
#ifdef AVHTTP_ENABLE_OPENSSL
	else if (protocol == "https")
	{
		m_protocol = "https";
	}
#endif
	else
	{
		ec = boost::asio::error::operation_not_supported;
		return;
	}

	// ����socket.
	if (protocol == "http")
	{
		m_sock.instantiate<nossl_socket>(m_io_service);
	}
#ifdef AVHTTP_ENABLE_OPENSSL
	else if (protocol == "https")
	{
		m_sock.instantiate<ssl_socket>(m_nossl_socket);

		// ����֤��·����֤��.
		ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
		if (!m_ca_directory.empty())
		{
			ssl_sock->add_verify_path(m_ca_directory, ec);
			if (ec)
			{
				return;
			}
		}
		if (!m_ca_cert.empty())
		{
			ssl_sock->load_verify_file(m_ca_cert, ec);
			if (ec)
			{
				return;
			}
		}
	}
#endif
	else
	{
		ec = boost::asio::error::operation_not_supported;
		return;
	}

	// ��ʼ��������.
	if (m_sock.instantiated() && !m_sock.is_open())
	{
		if (m_proxy.type == proxy_settings::none)
		{
			// ��ʼ�����˿ں�������.
			tcp::resolver resolver(m_io_service);
			std::ostringstream port_string;
			port_string << m_url.port();
			tcp::resolver::query query(m_url.host(), port_string.str());
			tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			tcp::resolver::iterator end;

			// �������ӽ��������ķ�������ַ.
			ec = boost::asio::error::host_not_found;
			while (ec && endpoint_iterator != end)
			{
				m_sock.close(ec);
				m_sock.connect(*endpoint_iterator++, ec);
			}
			if (ec)
			{
				return;
			}
		}
		else if (m_proxy.type == proxy_settings::socks5 ||
			m_proxy.type == proxy_settings::socks4 ||
			m_proxy.type == proxy_settings::socks5_pw)	// socks����.
		{
			if (protocol == "http")
			{
				socks_proxy_connect(m_sock, ec);
				if (ec)
				{
					return;
				}
			}
#ifdef AVHTTP_ENABLE_OPENSSL
			else if (protocol == "https")
			{
				socks_proxy_connect(m_nossl_socket, ec);
				if (ec)
				{
					return;
				}
				// ��ʼ����.
				ssl_socket* ssl_sock = m_sock.get<ssl_socket>();
				ssl_sock->handshake(ec);
				if (ec)
				{
					return;
				}
			}
#endif
			// �ʹ�������������������.
		}
		else if (m_proxy.type == proxy_settings::http ||
			m_proxy.type == proxy_settings::http_pw)		// http����.
		{
#ifdef AVHTTP_ENABLE_OPENSSL
			if (m_protocol == "https")
			{
				// https������.
				https_proxy_connect(m_nossl_socket, ec);
				if (ec)
				{
					return;
				}
				// ��ʼ����.
				ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
				ssl_sock->handshake(ec);
				if (ec)
				{
					return;
				}
			}
			else
#endif
				if (m_protocol == "http")
				{
					// ��ʼ�����˿ں�������.
					tcp::resolver resolver(m_io_service);
					std::ostringstream port_string;
					port_string << m_proxy.port;
					tcp::resolver::query query(m_proxy.hostname, port_string.str());
					tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
					tcp::resolver::iterator end;

					// �������ӽ��������Ĵ����������ַ.
					ec = boost::asio::error::host_not_found;
					while (ec && endpoint_iterator != end)
					{
						m_sock.close(ec);
						m_sock.connect(*endpoint_iterator++, ec);
					}
					if (ec)
					{
						return;
					}
				}
				else
				{
					// ��֧�ֵĲ�������.
					ec = boost::asio::error::operation_not_supported;
					return;
				}
		}
		else
		{
			// ��֧�ֵĲ�������.
			ec = boost::asio::error::operation_not_supported;
			return;
		}

		// ����Nagle��socket��.
		m_sock.set_option(tcp::no_delay(true), ec);
		if (ec)
		{
			return;
		}

#ifdef AVHTTP_ENABLE_OPENSSL
		if (m_protocol == "https")
		{
			// ��֤֤��.
			if (m_check_certificate)
			{
				ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
				if (X509 *cert = SSL_get_peer_certificate(ssl_sock->impl()->ssl))
				{
					long result = SSL_get_verify_result(ssl_sock->impl()->ssl);
					if (result == X509_V_OK)
					{
						if (certificate_matches_host(cert, m_url.host()))
							ec = boost::system::error_code();
						else
							ec = make_error_code(boost::system::errc::permission_denied);
					}
					else
						ec = make_error_code(boost::system::errc::permission_denied);
					X509_free(cert);
				}
				else
				{
					ec = make_error_code(boost::asio::error::invalid_argument);
				}

				if (ec)
				{
					return;
				}
			}
		}
#endif
	}
	else
	{
		// socket�Ѿ���.
		ec = boost::asio::error::already_open;
		return;
	}

	boost::system::error_code http_code;

	// ��������.
	request(m_request_opts, http_code);

	// �ж��Ƿ���Ҫ��ת.
	if (http_code == avhttp::errc::moved_permanently || http_code == avhttp::errc::found)
	{
		m_sock.close(ec);
		if (++m_redirects <= m_max_redirects)
		{
			open(m_location, ec);
			return;
		}
	}

	// ����ض������.
	m_redirects = 0;

	// ����http״̬��������.
	if (http_code)
		ec = http_code;
	else
		ec = boost::system::error_code();	// �򿪳ɹ�.

	return;
}

template <typename Handler>
void http_stream::async_open(const url &u, Handler handler)
{
	const std::string protocol = u.protocol();

	// ����url.
	m_url = u;

	// ���һЩѡ��.
	m_content_type = "";
	m_status_code = 0;
	m_content_length = 0;
	m_content_type = "";
	m_request.consume(m_request.size());
	m_response.consume(m_response.size());
	m_protocol = "";
	m_skip_crlf = true;

	// ��������url����.
	if (protocol == "http")
		m_protocol = "http";
#ifdef AVHTTP_ENABLE_OPENSSL
	else if (protocol == "https")
		m_protocol = "https";
#endif
	else
	{
		m_io_service.post(boost::asio::detail::bind_handler(
			handler, boost::asio::error::operation_not_supported));
		return;
	}

	// ����socket.
	if (protocol == "http")
	{
		m_sock.instantiate<nossl_socket>(m_io_service);
	}
#ifdef AVHTTP_ENABLE_OPENSSL
	else if (protocol == "https")
	{
		m_sock.instantiate<ssl_socket>(m_nossl_socket);

		// ����֤��·����֤��.
		boost::system::error_code ec;
		ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
		if (!m_ca_directory.empty())
		{
			ssl_sock->add_verify_path(m_ca_directory, ec);
			if (ec)
			{
				m_io_service.post(boost::asio::detail::bind_handler(
					handler, ec));
				return;
			}
		}
		if (!m_ca_cert.empty())
		{
			ssl_sock->load_verify_file(m_ca_cert, ec);
			if (ec)
			{
				m_io_service.post(boost::asio::detail::bind_handler(
					handler, ec));
				return;
			}
		}
	}
#endif
	else
	{
		m_io_service.post(boost::asio::detail::bind_handler(
			handler, boost::asio::error::operation_not_supported));
		return;
	}

	// �ж�socket�Ƿ��.
	if (m_sock.instantiated() && m_sock.is_open())
	{
		m_io_service.post(boost::asio::detail::bind_handler(
			handler, boost::asio::error::already_open));
		return;
	}

	// �첽socks�����ܴ���.
	if (m_proxy.type == proxy_settings::socks4 || m_proxy.type == proxy_settings::socks5
		|| m_proxy.type == proxy_settings::socks5_pw)
	{
		if (protocol == "http")
		{
			async_socks_proxy_connect(m_sock, handler);
		}
#ifdef AVHTTP_ENABLE_OPENSSL
		else if (protocol == "https")
		{
			async_socks_proxy_connect(m_nossl_socket, handler);
		}
#endif
		return;
	}

	std::string host;
	std::string port;
	if (m_proxy.type == proxy_settings::http || m_proxy.type == proxy_settings::http_pw)
	{
#ifdef AVHTTP_ENABLE_OPENSSL
		if (m_protocol == "https")
		{
			// https����.
			async_https_proxy_connect(m_nossl_socket, handler);
			return;
		}
		else
#endif
		{
			host = m_proxy.hostname;
			port = boost::lexical_cast<std::string>(m_proxy.port);
		}
	}
	else
	{
		host = m_url.host();
		port = boost::lexical_cast<std::string>(m_url.port());
	}

	// �����첽��ѯHOST.
	tcp::resolver::query query(host, port);

	// ��ʼ�첽��ѯHOST��Ϣ.
	typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
	HandlerWrapper h = handler;
	m_resolver.async_resolve(query,
		boost::bind(&http_stream::handle_resolve<HandlerWrapper>,
			this,
			boost::asio::placeholders::error,
			boost::asio::placeholders::iterator,
			h
		)
	);
}

template <typename MutableBufferSequence>
std::size_t http_stream::read_some(const MutableBufferSequence &buffers)
{
	boost::system::error_code ec;
	std::size_t bytes_transferred = read_some(buffers, ec);
	if (ec)
	{
		boost::throw_exception(boost::system::system_error(ec));
	}
	return bytes_transferred;
}

template <typename MutableBufferSequence>
std::size_t http_stream::read_some(const MutableBufferSequence &buffers,
	boost::system::error_code &ec)
{
	std::size_t bytes_transferred = 0;
	if (m_is_chunked)	// ��������˷ֿ鴫��ģʽ, ��������С, ����ȡС�ڿ��С������.
	{
		char crlf[2] = { '\r', '\n' };
		// chunked_size��СΪ0, ��ȡ��һ����ͷ��С.
		if (m_chunked_size == 0
#ifdef AVHTTP_ENABLE_ZLIB
			&& m_stream.avail_in == 0
#endif
			)
		{
			// �Ƿ�����CRLF, ����һ�ζ�ȡ��һ��������, �����ÿ��chunked����Ҫ��
			// ĩβ��CRLF����.
			if (!m_skip_crlf)
			{
				ec = boost::system::error_code();
				while (!ec && bytes_transferred != 2)
					bytes_transferred += read_some_impl(
						boost::asio::buffer(&crlf[bytes_transferred], 2 - bytes_transferred), ec);
				if (ec)
					return 0;
			}
			std::string hex_chunked_size;
			// ��ȡ.
			while (!ec)
			{
				char c;
				bytes_transferred = read_some_impl(boost::asio::buffer(&c, 1), ec);
				if (bytes_transferred == 1)
				{
					hex_chunked_size.push_back(c);
					std::size_t s = hex_chunked_size.size();
					if (s >= 2)
					{
						if (hex_chunked_size[s - 2] == crlf[0] && hex_chunked_size[s - 1] == crlf[1])
							break;
					}
				}
			}
			if (ec)
				return 0;

			// �õ�chunked size.
			std::stringstream ss;
			ss << std::hex << hex_chunked_size;
			ss >> m_chunked_size;

#ifdef AVHTTP_ENABLE_ZLIB
			if (!m_stream.zalloc)
			{
				if (inflateInit2(&m_stream, 32+15 ) != Z_OK)
				{
					ec = boost::asio::error::operation_not_supported;
					return 0;
				}
			}
#endif
			// chunked_size����������β��crlf, ����������β��crlfΪfalse״̬.
			m_skip_crlf = false;
		}

#ifdef AVHTTP_ENABLE_ZLIB
		if (m_chunked_size == 0 && m_is_gzip)
		{
			if (m_stream.avail_in == 0)
			{
				ec = boost::asio::error::eof;
				return 0;
			}
		}
#endif
		if (m_chunked_size != 0
#ifdef AVHTTP_ENABLE_ZLIB
			|| m_stream.avail_in != 0
#endif
			)	// ��ʼ��ȡchunked�е�����, �����ѹ��, ���ѹ���û����ܻ���.
		{
			std::size_t max_length = 0;
			{
				typename MutableBufferSequence::const_iterator iter = buffers.begin();
				typename MutableBufferSequence::const_iterator end = buffers.end();
				// ����õ��û�buffer_size�ܴ�С.
				for (; iter != end; ++iter)
				{
					boost::asio::mutable_buffer buffer(*iter);
					max_length += boost::asio::buffer_size(buffer);
				}
				// �õ����ʵĻ����С.
				max_length = std::min(max_length, m_chunked_size);
			}

#ifdef AVHTTP_ENABLE_ZLIB
			if (!m_is_gzip)	// ���û������gzip, ��ֱ�Ӷ�ȡ���ݺ󷵻�.
#endif
			{
				bytes_transferred = read_some_impl(boost::asio::buffer(buffers, max_length), ec);
				m_chunked_size -= bytes_transferred;
				return bytes_transferred;
			}
#ifdef AVHTTP_ENABLE_ZLIB
			else					// �����ȡ���ݵ���ѹ������.
			{
				if (m_stream.avail_in == 0)
				{
					std::size_t buf_size = std::min(m_chunked_size, std::size_t(1024));
					bytes_transferred = read_some_impl(boost::asio::buffer(m_zlib_buffer, buf_size), ec);
					m_chunked_size -= bytes_transferred;
					m_zlib_buffer_size = bytes_transferred;
					m_stream.avail_in = (uInt)m_zlib_buffer_size;
					m_stream.next_in = (z_const Bytef *)&m_zlib_buffer[0];
				}

				bytes_transferred = 0;

				{
					typename MutableBufferSequence::const_iterator iter = buffers.begin();
					typename MutableBufferSequence::const_iterator end = buffers.end();
					// ����õ��û�buffer_size�ܴ�С.
					for (; iter != end; ++iter)
					{
						boost::asio::mutable_buffer buffer(*iter);
						m_stream.next_in = (z_const Bytef *)(&m_zlib_buffer[0] + m_zlib_buffer_size - m_stream.avail_in);
						m_stream.avail_out = boost::asio::buffer_size(buffer);
						m_stream.next_out = boost::asio::buffer_cast<Bytef*>(buffer);
						int ret = inflate(&m_stream, Z_SYNC_FLUSH);
						if (ret < 0)
						{
							ec = boost::asio::error::operation_not_supported;
							return 0;
						}

						bytes_transferred += (boost::asio::buffer_size(buffer) - m_stream.avail_out);
						if (bytes_transferred != boost::asio::buffer_size(buffer))
							break;
					}
				}

				return bytes_transferred;
			}
#endif
		}

		if (m_chunked_size == 0)
			return 0;
	}

	// ���û������chunked.
#ifdef AVHTTP_ENABLE_ZLIB
	if (m_is_gzip && !m_is_chunked)
	{
		if (!m_stream.zalloc)
		{
			if (inflateInit2(&m_stream, 32+15 ) != Z_OK)
			{
				ec = boost::asio::error::operation_not_supported;
				return 0;
			}
		}

		if (m_stream.avail_in == 0)
		{
			bytes_transferred = read_some_impl(boost::asio::buffer(m_zlib_buffer, 1024), ec);
			m_zlib_buffer_size = bytes_transferred;
			m_stream.avail_in = (uInt)m_zlib_buffer_size;
			m_stream.next_in = (z_const Bytef *)&m_zlib_buffer[0];
		}

		bytes_transferred = 0;

		{
			typename MutableBufferSequence::const_iterator iter = buffers.begin();
			typename MutableBufferSequence::const_iterator end = buffers.end();
			// ����õ��û�buffer_size�ܴ�С.
			for (; iter != end; ++iter)
			{
				boost::asio::mutable_buffer buffer(*iter);
				m_stream.next_in = (z_const Bytef *)(&m_zlib_buffer[0] + m_zlib_buffer_size - m_stream.avail_in);
				m_stream.avail_out = boost::asio::buffer_size(buffer);
				m_stream.next_out = boost::asio::buffer_cast<Bytef*>(buffer);
				int ret = inflate(&m_stream, Z_SYNC_FLUSH);
				if (ret < 0)
				{
					ec = boost::asio::error::operation_not_supported;
					return 0;
				}

				bytes_transferred += (boost::asio::buffer_size(buffer) - m_stream.avail_out);
				if (bytes_transferred != boost::asio::buffer_size(buffer))
					break;
			}
		}

		return bytes_transferred;
	}
#endif

	bytes_transferred = read_some_impl(buffers, ec);
	return bytes_transferred;
}

template <typename MutableBufferSequence, typename Handler>
void http_stream::async_read_some(const MutableBufferSequence &buffers, Handler handler)
{
	BOOST_ASIO_READ_HANDLER_CHECK(Handler, handler) type_check;

	if (m_is_chunked)	// ��������˷ֿ鴫��ģʽ, ��������С, ����ȡС�ڿ��С������.
	{
		// chunked_size��СΪ0, ��ȡ��һ����ͷ��С, ���������gzip, ������ѹ���������ݲ�
		// ��ȡ��һ��chunkͷ.
		if (m_chunked_size == 0
#ifdef AVHTTP_ENABLE_ZLIB
			&& m_stream.avail_in == 0
#endif
			)
		{
			int bytes_transferred = 0;
			int response_size = m_response.size();

			// �Ƿ�����CRLF, ����һ�ζ�ȡ��һ��������, �����ÿ��chunked����Ҫ��
			// ĩβ��CRLF����.
			if (!m_skip_crlf)
			{
				boost::shared_array<char> crlf(new char[2]);
				memset((void*)crlf.get(), 0, 2);

				if (response_size > 0)	// ��m_response����������.
				{
					bytes_transferred = m_response.sgetn(
						crlf.get(), std::min(response_size, 2));
					if (bytes_transferred == 1)
					{
						// �����첽��ȡ��һ��LF�ֽ�.
						typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
						HandlerWrapper h(handler);
						m_sock.async_read_some(boost::asio::buffer(&crlf.get()[1], 1),
							boost::bind(&http_stream::handle_skip_crlf<MutableBufferSequence, HandlerWrapper>,
								this, buffers, h, crlf,
								boost::asio::placeholders::error,
								boost::asio::placeholders::bytes_transferred
							)
						);
						return;
					}
					else
					{
						// ��ȡ��CRLF, so, ����ֻ����2!!! Ȼ��ʼ����chunked size.
						BOOST_ASSERT(bytes_transferred == 2);
						BOOST_ASSERT(crlf.get()[0] == '\r' && crlf.get()[1] == '\n');
					}
				}
				else
				{
					// �첽��ȡCRLF.
					typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
					HandlerWrapper h(handler);
					m_sock.async_read_some(boost::asio::buffer(&crlf.get()[0], 2),
						boost::bind(&http_stream::handle_skip_crlf<MutableBufferSequence, HandlerWrapper>,
							this, buffers, h, crlf,
							boost::asio::placeholders::error,
							boost::asio::placeholders::bytes_transferred
						)
					);
					return;
				}
			}

			// ����CRLF, ��ʼ��ȡchunked size.
			typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
			HandlerWrapper h(handler);
			boost::asio::async_read_until(m_sock, m_response, "\r\n",
				boost::bind(&http_stream::handle_chunked_size<MutableBufferSequence, HandlerWrapper>,
					this, buffers, h,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred
				)
			);
			return;
		}
		else
		{
			std::size_t max_length = 0;

			// ����Ϊ0��ֱ�Ӷ�ȡm_response�е�����, �����ٴ�socket��ȡ����, ����
			// ��ȡ���ݵ�β����ʱ��, ������ʱ��ȴ������.
			if (m_response.size() != 0)
				max_length = 0;
			else
			{
				typename MutableBufferSequence::const_iterator iter = buffers.begin();
				typename MutableBufferSequence::const_iterator end = buffers.end();
				// ����õ��û�buffer_size�ܴ�С.
				for (; iter != end; ++iter)
				{
					boost::asio::mutable_buffer buffer(*iter);
					max_length += boost::asio::buffer_size(buffer);
				}
				// �õ����ʵĻ����С.
				max_length = std::min(max_length, m_chunked_size);
			}

			// ��ȡ���ݵ�m_response, �����ѹ��, ��Ҫ��handle_async_read�н�ѹ.
			boost::asio::streambuf::mutable_buffers_type bufs = m_response.prepare(max_length);
			typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
			HandlerWrapper h(handler);
			m_sock.async_read_some(boost::asio::buffer(bufs),
				boost::bind(&http_stream::handle_async_read<MutableBufferSequence, HandlerWrapper>,
					this, buffers, h,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred
				)
			);
			return;
		}
	}

	boost::system::error_code ec;
	if (m_response.size() > 0)
	{
		std::size_t bytes_transferred = read_some(buffers, ec);
		m_io_service.post(
			boost::asio::detail::bind_handler(handler, ec, bytes_transferred));
		return;
	}

	// �����������ݲ���, ֱ�Ӵ�socket���첽��ȡ.
	m_sock.async_read_some(buffers, handler);
}

template <typename ConstBufferSequence>
std::size_t http_stream::write_some(const ConstBufferSequence &buffers)
{
	boost::system::error_code ec;
	std::size_t bytes_transferred = write_some(buffers, ec);
	if (ec)
	{
		boost::throw_exception(boost::system::system_error(ec));
	}
	return bytes_transferred;
}

template <typename ConstBufferSequence>
std::size_t http_stream::write_some(const ConstBufferSequence &buffers,
	boost::system::error_code &ec)
{
	std::size_t bytes_transferred = m_sock.write_some(buffers, ec);
	if (ec == boost::asio::error::shut_down)
		ec = boost::asio::error::eof;
	return bytes_transferred;
}

template <typename ConstBufferSequence, typename Handler>
void http_stream::async_write_some(const ConstBufferSequence &buffers, Handler handler)
{
	BOOST_ASIO_WAIT_HANDLER_CHECK(Handler, handler) type_check;

	m_sock.async_write_some(buffers, handler);
}

void http_stream::request(request_opts &opt)
{
	boost::system::error_code ec;
	request(opt, ec);
	if (ec)
	{
		boost::throw_exception(boost::system::system_error(ec));
	}
}

void http_stream::request(request_opts &opt, boost::system::error_code &ec)
{
	request_impl<socket_type>(m_sock, opt, ec);
}

template <typename Handler>
void http_stream::async_request(const request_opts &opt, Handler handler)
{
	boost::system::error_code ec;

	// �ж�socket�Ƿ��.
	if (!m_sock.is_open())
	{
		handler(boost::asio::error::network_reset);
		return;
	}

	// ���浽һ���µ�opts�в���.
	request_opts opts = opt;

	// �õ�urlѡ��.
	std::string new_url;
	if (opts.find(http_options::url, new_url))
		opts.remove(http_options::url);		// ɾ���������ѡ��.

	if (!new_url.empty())
	{
		BOOST_ASSERT(url::from_string(new_url).host() == m_url.host());	// ������ͬһ����.
		m_url = new_url;
	}

	// �õ�request_method.
	std::string request_method = "GET";
	if (opts.find(http_options::request_method, request_method))
		opts.remove(http_options::request_method);	// ɾ���������ѡ��.

	// �õ�http�İ汾��Ϣ.
	std::string http_version = "HTTP/1.1";
	if (opts.find(http_options::http_version, http_version))
		opts.remove(http_options::http_version);	// ɾ���������ѡ��.

	// �õ�Host��Ϣ.
	std::string host = m_url.to_string(url::host_component | url::port_component);
	if (opts.find(http_options::host, host))
		opts.remove(http_options::host);	// ɾ���������ѡ��.

	// �õ�Accept��Ϣ.
	std::string accept = "text/html, application/xhtml+xml, */*";
	if (opts.find(http_options::accept, accept))
		opts.remove(http_options::accept);	// ɾ���������ѡ��.

	// ���user_agent.
	std::string user_agent = "avhttp/2.1";
	if (opts.find(http_options::user_agent, user_agent))
		opts.remove(http_options::user_agent);	// ɾ���������ѡ��.

	// Ĭ�����close.
	std::string connection = "close";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
	{
		if (opts.find(http_options::proxy_connection, connection))
			opts.remove(http_options::proxy_connection);		// ɾ���������ѡ��.
	}
	else
	{
		if (opts.find(http_options::connection, connection))
			opts.remove(http_options::connection);		// ɾ���������ѡ��.
	}

	// �Ƿ����bodyѡ��.
	std::string body;
	if (opts.find(http_options::request_body, body))
		opts.remove(http_options::request_body);	// ɾ���������ѡ��.

	// ѭ����������ѡ��.
	std::string other_option_string;
	request_opts::option_item_list &list = opts.option_all();
	for (request_opts::option_item_list::iterator val = list.begin(); val != list.end(); val++)
	{
		other_option_string += (val->first + ": " + val->second + "\r\n");
	}

	// ���ϸ�ѡ�Http�����ַ�����.
	std::string request_string;
	m_request.consume(m_request.size());
	std::ostream request_stream(&m_request);
	request_stream << request_method << " ";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
		request_stream << m_url.to_string().c_str();
	else
		request_stream << m_url.to_string(url::path_component | url::query_component);
	request_stream << " " << http_version << "\r\n";
	request_stream << "Host: " << host << "\r\n";
	request_stream << "Accept: " << accept << "\r\n";
	request_stream << "User-Agent: " << user_agent << "\r\n";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
		request_stream << "Proxy-Connection: " << connection << "\r\n";
	else
		request_stream << "Connection: " << connection << "\r\n";
	request_stream << other_option_string << "\r\n";
	if (!body.empty())
	{
		request_stream << body;
	}

	// �첽��������.
	typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
	boost::asio::async_write(m_sock, m_request, boost::asio::transfer_exactly(m_request.size()),
		boost::bind(&http_stream::handle_request<HandlerWrapper>,
			this, HandlerWrapper(handler),
			boost::asio::placeholders::error
		)
	);
}

void http_stream::clear()
{
	m_request.consume(m_request.size());
	m_response.consume(m_response.size());
}

void http_stream::close()
{
	boost::system::error_code ec;
	close(ec);
	if (ec)
	{
		boost::throw_exception(boost::system::system_error(ec));
	}
}

void http_stream::close(boost::system::error_code &ec)
{
	ec = boost::system::error_code();

	if (is_open())
	{
		// �ر�socket.
		m_sock.close(ec);

		// ����ڲ��ĸ��ֻ�����Ϣ.
		m_request.consume(m_request.size());
		m_response.consume(m_response.size());
		m_content_type.clear();
		m_location.clear();
		m_protocol.clear();
	}
}

bool http_stream::is_open() const
{
	return m_sock.is_open();
}

boost::asio::io_service& http_stream::get_io_service()
{
	return m_io_service;
}

void http_stream::max_redirects(int n)
{
	m_max_redirects = n;
}

void http_stream::proxy(const proxy_settings &s)
{
	m_proxy = s;
}

void http_stream::request_options(const request_opts &options)
{
	m_request_opts = options;
}

request_opts http_stream::request_options(void) const
{
	return m_request_opts;
}

response_opts http_stream::response_options(void) const
{
	return m_response_opts;
}

const std::string& http_stream::location() const
{
	return m_location;
}

boost::int64_t http_stream::content_length()
{
	return m_content_length;
}

void http_stream::check_certificate(bool is_check)
{
#ifdef AVHTTP_ENABLE_OPENSSL
	m_check_certificate = is_check;
#endif
}

void http_stream::add_verify_path(const std::string &path)
{
	m_ca_directory = path;
	return;
}

void http_stream::load_verify_file(const std::string &filename)
{
	m_ca_cert = filename;
	return;
}


// ����Ϊ�ڲ����ʵ��, �ǽӿ�.

template <typename MutableBufferSequence>
std::size_t http_stream::read_some_impl(const MutableBufferSequence &buffers,
	boost::system::error_code &ec)
{
	// �������������m_response��, �ȶ�ȡm_response�е�����.
	if (m_response.size() > 0)
	{
		std::size_t bytes_transferred = 0;
		typename MutableBufferSequence::const_iterator iter = buffers.begin();
		typename MutableBufferSequence::const_iterator end = buffers.end();
		for (; iter != end && m_response.size() > 0; ++iter)
		{
			boost::asio::mutable_buffer buffer(*iter);
			std::size_t length = boost::asio::buffer_size(buffer);
			if (length > 0)
			{
				bytes_transferred += m_response.sgetn(
					boost::asio::buffer_cast<char*>(buffer), length);
			}
		}
		ec = boost::system::error_code();
		return bytes_transferred;
	}

	// �ٴ�socket�ж�ȡ����.
	std::size_t bytes_transferred = m_sock.read_some(buffers, ec);
	if (ec == boost::asio::error::shut_down)
		ec = boost::asio::error::eof;
	return bytes_transferred;
}

template <typename Handler>
void http_stream::handle_resolve(const boost::system::error_code &err,
	tcp::resolver::iterator endpoint_iterator, Handler handler)
{
	if (!err)
	{
		// �����첽����.
		// !!!��ע: ����m_sock������ssl, ��ô���ӵ��������ʵ�ֱ���װ��ssl_stream
		// ��, ����, �����Ҫʹ��boost::asio::async_connect�Ļ�, ��Ҫ��http_stream
		// ��ʵ�����ֲ���, ���򽫻�õ�һ������.
		m_sock.async_connect(tcp::endpoint(*endpoint_iterator),
			boost::bind(&http_stream::handle_connect<Handler>,
				this, handler, endpoint_iterator,
				boost::asio::placeholders::error
			)
		);
	}
	else
	{
		// ����ص�.
		handler(err);
	}
}

template <typename Handler>
void http_stream::handle_connect(Handler handler,
	tcp::resolver::iterator endpoint_iterator, const boost::system::error_code &err)
{
	if (!err)
	{
#ifdef AVHTTP_ENABLE_OPENSSL
		if (m_protocol == "https")
		{
			// ��֤֤��.
			boost::system::error_code ec;
			if (m_check_certificate)
			{
				ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
				if (X509 *cert = SSL_get_peer_certificate(ssl_sock->impl()->ssl))
				{
					long result = SSL_get_verify_result(ssl_sock->impl()->ssl);
					if (result == X509_V_OK)
					{
						if (certificate_matches_host(cert, m_url.host()))
							ec = boost::system::error_code();
						else
							ec = make_error_code(boost::system::errc::permission_denied);
					}
					else
						ec = make_error_code(boost::system::errc::permission_denied);
					X509_free(cert);
				}
				else
				{
					ec = make_error_code(boost::asio::error::invalid_argument);
				}
			}

			if (ec)
			{
				handler(ec);
				return;
			}
		}
#endif
		// �����첽����.
		async_request(m_request_opts, handler);
	}
	else
	{
		// ����Ƿ��Ѿ�������endpoint�б��е�����endpoint.
		if (++endpoint_iterator == tcp::resolver::iterator())
			handler(err);
		else
		{
			// ���������첽����.
			// !!!��ע: ����m_sock������ssl, ��ô���ӵ��������ʵ�ֱ���װ��ssl_stream
			// ��, ����, �����Ҫʹ��boost::asio::async_connect�Ļ�, ��Ҫ��http_stream
			// ��ʵ�����ֲ���, ���򽫻�õ�һ������.
			m_sock.async_connect(tcp::endpoint(*endpoint_iterator),
				boost::bind(&http_stream::handle_connect<Handler>,
					this, handler, endpoint_iterator,
					boost::asio::placeholders::error
				)
			);
		}
	}
}

template <typename Handler>
void http_stream::handle_request(Handler handler, const boost::system::error_code &err)
{
	// ��������.
	if (err)
	{
		handler(err);
		return;
	}

	// �첽��ȡHttp status.
	boost::asio::async_read_until(m_sock, m_response, "\r\n",
		boost::bind(&http_stream::handle_status<Handler>,
			this, handler,
			boost::asio::placeholders::error
		)
	);
}

template <typename Handler>
void http_stream::handle_status(Handler handler, const boost::system::error_code &err)
{
	// ��������.
	if (err)
	{
		handler(err);
		return;
	}

	// ���Ƶ��µ�streambuf�д�������http״̬, �������http״̬��, ��ô������m_response�е�����,
	// ����Ҫ��Ϊ�˼��ݷǱ�׼http������ֱ����ͻ��˷����ļ�����Ҫ, ������Ȼ��Ҫ��malformed_status_line
	// ֪ͨ�û�, ����m_response�е�������δ���, ���û��Լ������Ƿ��ȡ.
	boost::asio::streambuf tempbuf;
	int response_size = m_response.size();
	boost::asio::streambuf::const_buffers_type::const_iterator begin(m_response.data().begin());
	const char* ptr = boost::asio::buffer_cast<const char*>(*begin);
	std::ostream tempbuf_stream(&tempbuf);
	tempbuf_stream.write(ptr, response_size);

	// ���http״̬��, version_major��version_minor��httpЭ��İ汾��.
	int version_major = 0;
	int version_minor = 0;
	m_status_code = 0;
	if (!detail::parse_http_status_line(
		std::istreambuf_iterator<char>(&tempbuf),
		std::istreambuf_iterator<char>(),
		version_major, version_minor, m_status_code))
	{
		handler(avhttp::errc::malformed_status_line);
		return;
	}

	// �����״̬����ռ�õ��ֽ���.
	m_response.consume(response_size - tempbuf.size());

	// "continue"��ʾ������Ҫ�����ȴ�����״̬.
	if (m_status_code == avhttp::errc::continue_request)
	{
		boost::asio::async_read_until(m_sock, m_response, "\r\n",
			boost::bind(&http_stream::handle_status<Handler>,
				this, handler,
				boost::asio::placeholders::error
			)
		);
	}
	else
	{
		// ���ԭ�еķ���ѡ��.
		m_response_opts.clear();
		// ���״̬��.
		m_response_opts.insert("_status_code", boost::str(boost::format("%d") % m_status_code));

		// �첽��ȡ����Http header����.
		boost::asio::async_read_until(m_sock, m_response, "\r\n\r\n",
			boost::bind(&http_stream::handle_header<Handler>,
				this, handler,
				boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error
			)
		);
	}
}

template <typename Handler>
void http_stream::handle_header(Handler handler, int bytes_transferred, const boost::system::error_code &err)
{
	if (err)
	{
		handler(err);
		return;
	}

	std::string header_string;
	header_string.resize(bytes_transferred);
	m_response.sgetn(&header_string[0], bytes_transferred);

	// ����Http Header.
	if (!detail::parse_http_headers(header_string.begin(), header_string.end(),
		m_content_type, m_content_length, m_location, m_response_opts.option_all()))
	{
		handler(avhttp::errc::malformed_response_headers);
		return;
	}
	boost::system::error_code ec;

	// �ж��Ƿ���Ҫ��ת.
	if (m_status_code == avhttp::errc::moved_permanently || m_status_code == avhttp::errc::found)
	{
		m_sock.close(ec);
		if (++m_redirects <= m_max_redirects)
		{
			async_open(m_location, handler);
			return;
		}
	}

	// ����ض������.
	m_redirects = 0;

	if (m_status_code != avhttp::errc::ok && m_status_code != avhttp::errc::partial_content)
		ec = make_error_code(static_cast<avhttp::errc::errc_t>(m_status_code));

	// �����Ƿ�������gzѹ��.
	std::string encoding = m_response_opts.find(http_options::content_encoding);
#ifdef AVHTTP_ENABLE_ZLIB
	if (encoding == "gzip" || encoding == "x-gzip")
		m_is_gzip = true;
#endif
	encoding = m_response_opts.find(http_options::transfer_encoding);
	if (encoding == "chunked")
		m_is_chunked = true;

	// �ص�֪ͨ.
	handler(ec);
}

template <typename MutableBufferSequence, typename Handler>
void http_stream::handle_skip_crlf(const MutableBufferSequence &buffers,
	Handler handler, boost::shared_array<char> crlf,
	const boost::system::error_code &ec, std::size_t bytes_transferred)
{
	if (!ec)
	{
		BOOST_ASSERT(crlf.get()[0] == '\r' && crlf.get()[1] == '\n');
		// ����CRLF, ��ʼ��ȡchunked size.
		typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
		HandlerWrapper h(handler);
		boost::asio::async_read_until(m_sock, m_response, "\r\n",
			boost::bind(&http_stream::handle_chunked_size<MutableBufferSequence, HandlerWrapper>,
				this, buffers, h,
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred
			)
		);
		return;
	}
	else
	{
		handler(ec, bytes_transferred);
	}
}

template <typename MutableBufferSequence, typename Handler>
void http_stream::handle_async_read(const MutableBufferSequence &buffers,
	Handler handler, const boost::system::error_code &ec, std::size_t bytes_transferred)
{
	boost::system::error_code err;

	if (!ec || m_response.size() > 0)
	{
		// �ύ����.
		m_response.commit(bytes_transferred);

#ifdef AVHTTP_ENABLE_ZLIB
		if (!m_is_gzip)	// ���û������gzip, ��ֱ�Ӷ�ȡ���ݺ󷵻�.
#endif
		{
			bytes_transferred = read_some_impl(boost::asio::buffer(buffers, m_chunked_size), err);
			m_chunked_size -= bytes_transferred;
			handler(err, bytes_transferred);
			return;
		}
#ifdef AVHTTP_ENABLE_ZLIB
		else					// �����ȡ���ݵ���ѹ������.
		{
			if (m_stream.avail_in == 0)
			{
				std::size_t buf_size = std::min(m_chunked_size, std::size_t(1024));
				bytes_transferred = read_some_impl(boost::asio::buffer(m_zlib_buffer, buf_size), err);
				m_chunked_size -= bytes_transferred;
				m_zlib_buffer_size = bytes_transferred;
				m_stream.avail_in = (uInt)m_zlib_buffer_size;
				m_stream.next_in = (z_const Bytef *)&m_zlib_buffer[0];
			}

			bytes_transferred = 0;

			{
				typename MutableBufferSequence::const_iterator iter = buffers.begin();
				typename MutableBufferSequence::const_iterator end = buffers.end();
				// ����õ��û�buffer_size�ܴ�С.
				for (; iter != end; ++iter)
				{
					boost::asio::mutable_buffer buffer(*iter);
					m_stream.next_in = (z_const Bytef *)(&m_zlib_buffer[0] + m_zlib_buffer_size - m_stream.avail_in);
					m_stream.avail_out = boost::asio::buffer_size(buffer);
					m_stream.next_out = boost::asio::buffer_cast<Bytef*>(buffer);
					int ret = inflate(&m_stream, Z_SYNC_FLUSH);
					if (ret < 0)
					{
						err = boost::asio::error::operation_not_supported;
						// ��ѹ��������, ֪ͨ�û�����������.
						handler(err, 0);
						return;
					}

					bytes_transferred += (boost::asio::buffer_size(buffer) - m_stream.avail_out);
					if (bytes_transferred != boost::asio::buffer_size(buffer))
						break;
				}
			}

			if (m_chunked_size == 0 && m_stream.avail_in == 0)
				err = ec;

			handler(err, bytes_transferred);
			return;
		}
#endif
	}
	else
	{
		handler(ec, bytes_transferred);
	}
}

template <typename MutableBufferSequence, typename Handler>
void http_stream::handle_chunked_size(const MutableBufferSequence &buffers,
	Handler handler, const boost::system::error_code &ec, std::size_t bytes_transferred)
{
	if (!ec)
	{
		// ����m_response�е�chunked size.
		std::string hex_chunked_size;
		boost::system::error_code err;
		while (!err && m_response.size() > 0)
		{
			char c;
			bytes_transferred = read_some_impl(boost::asio::buffer(&c, 1), err);
			if (bytes_transferred == 1)
			{
				hex_chunked_size.push_back(c);
				std::size_t s = hex_chunked_size.size();
				if (s >= 2)
				{
					if (hex_chunked_size[s - 2] == '\r' && hex_chunked_size[s - 1] == '\n')
						break;
				}
			}
		}
		BOOST_ASSERT(!err);
		// �õ�chunked size.
		std::stringstream ss;
		ss << std::hex << hex_chunked_size;
		ss >> m_chunked_size;

#ifdef AVHTTP_ENABLE_ZLIB // ��ʼ��ZLIB��, ÿ�ν�ѹÿ��chunked��ʱ��, ����Ҫ���³�ʼ��.
		if (!m_stream.zalloc)
		{
			if (inflateInit2(&m_stream, 32+15 ) != Z_OK)
			{
				handler(make_error_code(boost::asio::error::operation_not_supported), 0);
				return;
			}
		}
#endif
		// chunked_size����������β��crlf, ����������β��crlfΪfalse״̬.
		m_skip_crlf = false;

		// ��ȡ����.
		if (m_chunked_size != 0)	// ��ʼ��ȡchunked�е�����, �����ѹ��, ���ѹ���û����ܻ���.
		{
			std::size_t max_length = 0;

			if (m_response.size() != 0)
				max_length = 0;
			else
			{
				typename MutableBufferSequence::const_iterator iter = buffers.begin();
				typename MutableBufferSequence::const_iterator end = buffers.end();
				// ����õ��û�buffer_size�ܴ�С.
				for (; iter != end; ++iter)
				{
					boost::asio::mutable_buffer buffer(*iter);
					max_length += boost::asio::buffer_size(buffer);
				}
				// �õ����ʵĻ����С.
				max_length = std::min(max_length, m_chunked_size);
			}

			// ��ȡ���ݵ�m_response, �����ѹ��, ��Ҫ��handle_async_read�н�ѹ.
			boost::asio::streambuf::mutable_buffers_type bufs = m_response.prepare(max_length);
			typedef boost::function<void (boost::system::error_code, std::size_t)> HandlerWrapper;
			HandlerWrapper h(handler);
			m_sock.async_read_some(boost::asio::buffer(bufs),
				boost::bind(&http_stream::handle_async_read<MutableBufferSequence, HandlerWrapper>,
					this, buffers, h,
					boost::asio::placeholders::error,
					boost::asio::placeholders::bytes_transferred
				)
			);
			return;
		}

		if (m_chunked_size == 0)
		{
			boost::system::error_code err = make_error_code(boost::asio::error::eof);
			handler(err, 0);
			return;
		}
	}

	// ��������, ֪ͨ�ϲ����.
	handler(ec, 0);
}

template <typename Stream>
void http_stream::socks_proxy_connect(Stream &sock, boost::system::error_code &ec)
{
	using namespace avhttp::detail;

	const proxy_settings &s = m_proxy;

	// ��ʼ��������Ķ˿ں�������.
	tcp::resolver resolver(m_io_service);
	std::ostringstream port_string;
	port_string << s.port;
	tcp::resolver::query query(s.hostname.c_str(), port_string.str());
	tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ec);
	tcp::resolver::iterator end;

	// �������ʧ��, �򷵻�.
	if (ec)
		return;

	// �������ӽ��������ķ�������ַ.
	ec = boost::asio::error::host_not_found;
	while (ec && endpoint_iterator != end)
	{
		sock.close(ec);
		sock.connect(*endpoint_iterator++, ec);
	}
	if (ec)
	{
		return;
	}

	if (s.type == proxy_settings::socks5 || s.type == proxy_settings::socks5_pw)
	{
		// ���Ͱ汾��Ϣ.
		{
			m_request.consume(m_request.size());

			std::size_t bytes_to_write = s.username.empty() ? 3 : 4;
			boost::asio::mutable_buffer b = m_request.prepare(bytes_to_write);
			char *p = boost::asio::buffer_cast<char*>(b);
			write_uint8(5, p); // SOCKS VERSION 5.
			if (s.username.empty())
			{
				write_uint8(1, p); // 1 authentication method (no auth)
				write_uint8(0, p); // no authentication
			}
			else
			{
				write_uint8(2, p); // 2 authentication methods
				write_uint8(0, p); // no authentication
				write_uint8(2, p); // username/password
			}
			m_request.commit(bytes_to_write);
			boost::asio::write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write), ec);
			if (ec)
				return;
		}

		// ��ȡ�汾��Ϣ.
		m_response.consume(m_response.size());
		boost::asio::read(sock, m_response, boost::asio::transfer_exactly(2), ec);
		if (ec)
			return;

		int version, method;
		{
			boost::asio::const_buffer b = m_response.data();
			const char *p = boost::asio::buffer_cast<const char*>(b);
			version = read_uint8(p);
			method = read_uint8(p);
			if (version != 5)	// �汾������5, ��֧��socks5.
			{
				ec = make_error_code(errc::socks_unsupported_version);
				return;
			}
		}
		if (method == 2)
		{
			if (s.username.empty())
			{
				ec = make_error_code(errc::socks_username_required);
				return;
			}

			// start sub-negotiation.
			m_request.consume(m_request.size());
			std::size_t bytes_to_write = s.username.size() + s.password.size() + 3;
			boost::asio::mutable_buffer b = m_request.prepare(bytes_to_write);
			char *p = boost::asio::buffer_cast<char*>(b);
			write_uint8(1, p);
			write_uint8(s.username.size(), p);
			write_string(s.username, p);
			write_uint8(s.password.size(), p);
			write_string(s.password, p);
			m_request.commit(bytes_to_write);

			// �����û�������Ϣ.
			boost::asio::write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write), ec);
			if (ec)
				return;

			// ��ȡ״̬.
			m_response.consume(m_response.size());
			boost::asio::read(sock, m_response, boost::asio::transfer_exactly(2), ec);
			if (ec)
				return;
		}
		else if (method == 0)
		{
			socks_proxy_handshake(sock, ec);
			return;
		}

		{
			// ��ȡ�汾״̬.
			boost::asio::const_buffer b = m_response.data();
			const char *p = boost::asio::buffer_cast<const char*>(b);

			int version = read_uint8(p);
			int status = read_uint8(p);

			// ��֧�ֵ���֤�汾.
			if (version != 1)
			{
				ec = make_error_code(errc::socks_unsupported_authentication_version);
				return;
			}

			// ��֤����.
			if (status != 0)
			{
				ec = make_error_code(errc::socks_authentication_error);
				return;
			}

			socks_proxy_handshake(sock, ec);
		}
	}
	else if (s.type == proxy_settings::socks4)
	{
		socks_proxy_handshake(sock, ec);
	}
}

template <typename Stream>
void http_stream::socks_proxy_handshake(Stream &sock, boost::system::error_code &ec)
{
	using namespace avhttp::detail;

	const url &u = m_url;
	const proxy_settings &s = m_proxy;

	m_request.consume(m_request.size());
	std::string host = u.host();
	std::size_t bytes_to_write = 7 + host.size();
	if (s.type == proxy_settings::socks4)
		bytes_to_write = 9 + s.username.size();
	boost::asio::mutable_buffer mb = m_request.prepare(bytes_to_write);
	char *wp = boost::asio::buffer_cast<char*>(mb);

	if (s.type == proxy_settings::socks5 || s.type == proxy_settings::socks5_pw)
	{
		// ����socks5��������.
		write_uint8(5, wp); // SOCKS VERSION 5.
		write_uint8(1, wp); // CONNECT command.
		write_uint8(0, wp); // reserved.
		write_uint8(3, wp); // address type.
		BOOST_ASSERT(host.size() <= 255);
		write_uint8(host.size(), wp);				// domainname size.
		std::copy(host.begin(), host.end(),wp);		// domainname.
		wp += host.size();
		write_uint16(u.port(), wp);					// port.
	}
	else if (s.type == proxy_settings::socks4)
	{
		write_uint8(4, wp); // SOCKS VERSION 4.
		write_uint8(1, wp); // CONNECT command.
		// socks4Э��ֻ����ip��ַ, ��֧������.
		tcp::resolver resolver(m_io_service);
		std::ostringstream port_string;
		port_string << u.port();
		tcp::resolver::query query(host.c_str(), port_string.str());
		// �����������е�ip��ַ.
		unsigned long ip = resolver.resolve(query, ec)->endpoint().address().to_v4().to_ulong();
		write_uint16(u.port(), wp);	// port.
		write_uint32(ip, wp);		// ip address.
		// username.
		if (!s.username.empty())
		{
			std::copy(s.username.begin(), s.username.end(), wp);
			wp += s.username.size();
		}
		// NULL terminator.
		write_uint8(0, wp);
	}
	else
	{
		ec = make_error_code(errc::socks_unsupported_version);
		return;
	}

	// ����.
	m_request.commit(bytes_to_write);
	boost::asio::write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write), ec);
	if (ec)
		return;

	// ����socks����������.
	std::size_t bytes_to_read = 0;
	if (s.type == proxy_settings::socks5 || s.type == proxy_settings::socks5_pw)
		bytes_to_read = 10;
	else if (s.type == proxy_settings::socks4)
		bytes_to_read = 8;

	BOOST_ASSERT(bytes_to_read == 0);

	m_response.consume(m_response.size());
	boost::asio::read(sock, m_response,
		boost::asio::transfer_exactly(bytes_to_read), ec);

	// ��������������.
	boost::asio::const_buffer cb = m_response.data();
	const char *rp = boost::asio::buffer_cast<const char*>(cb);
	int version = read_uint8(rp);
	int response = read_uint8(rp);

	if (version == 5)
	{
		if (s.type != proxy_settings::socks5 && s.type != proxy_settings::socks5_pw)
		{
			// �����socksЭ�鲻��sock5.
			ec = make_error_code(errc::socks_unsupported_version);
			return;
		}

		if (response != 0)
		{
			ec = make_error_code(errc::socks_general_failure);
			// �õ�����ϸ�Ĵ�����Ϣ.
			switch (response)
			{
			case 2: ec = boost::asio::error::no_permission; break;
			case 3: ec = boost::asio::error::network_unreachable; break;
			case 4: ec = boost::asio::error::host_unreachable; break;
			case 5: ec = boost::asio::error::connection_refused; break;
			case 6: ec = boost::asio::error::timed_out; break;
			case 7: ec = make_error_code(errc::socks_command_not_supported); break;
			case 8: ec = boost::asio::error::address_family_not_supported; break;
			}
			return;
		}

		rp++;	// skip reserved.
		int atyp = read_uint8(rp);	// atyp.

		if (atyp == 1)		// address / port ��ʽ����.
		{
			m_response.consume(m_response.size());
			ec = boost::system::error_code();	// û�з�������, ����.
			return;
		}
		else if (atyp == 3)	// domainname ����.
		{
			int len = read_uint8(rp);	// ��ȡdomainname����.
			bytes_to_read = len - 3;
			// ������ȡ.
			m_response.commit(boost::asio::read(sock,
				m_response.prepare(bytes_to_read), boost::asio::transfer_exactly(bytes_to_read), ec));
			// if (ec)
			//	return;
			//
			// �õ�domainname.
			// std::string domain;
			// domain.resize(len);
			// std::copy(rp, rp + len, domain.begin());
			m_response.consume(m_response.size());
			ec = boost::system::error_code();
			return;
		}
		// else if (atyp == 4)	// ipv6 ����, ����ʵ��!
		// {
		//	ec = boost::asio::error::address_family_not_supported;
		//	return;
		// }
		else
		{
			ec = boost::asio::error::address_family_not_supported;
			return;
		}
	}
	else if (version == 4)
	{
		// 90: request granted.
		// 91: request rejected or failed.
		// 92: request rejected becasue SOCKS server cannot connect to identd on the client.
		// 93: request rejected because the client program and identd report different user-ids.
		if (response == 90)	// access granted.
		{
			m_response.consume(m_response.size());
			ec = boost::system::error_code();
			return;
		}
		else
		{
			ec = errc::socks_general_failure;
			switch (response)
			{
			case 91: ec = errc::socks_authentication_error; break;
			case 92: ec = errc::socks_no_identd; break;
			case 93: ec = errc::socks_identd_error; break;
			}
			return;
		}
	}
	else
	{
		ec = errc::socks_general_failure;
		return;
	}
}

// socks��������첽����.
template <typename Stream, typename Handler>
void http_stream::async_socks_proxy_connect(Stream &sock, Handler handler)
{
	// �����첽��ѯproxy������Ϣ.
	std::ostringstream port_string;
	port_string << m_proxy.port;
	tcp::resolver::query query(m_proxy.hostname, port_string.str());

	m_proxy_status = socks_proxy_resolve;

	// ��ʼ�첽��������Ķ˿ں�������.
	typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
	m_resolver.async_resolve(query,
		boost::bind(&http_stream::async_socks_proxy_resolve<Stream, HandlerWrapper>,
			this,
			boost::asio::placeholders::error,
			boost::asio::placeholders::iterator,
			boost::ref(sock), HandlerWrapper(handler)
		)
	);
}

// �첽�����ѯ�ص�.
template <typename Stream, typename Handler>
void http_stream::async_socks_proxy_resolve(const boost::system::error_code &err,
	tcp::resolver::iterator endpoint_iterator, Stream &sock, Handler handler)
{
	if (err)
	{
		handler(err);
		return;
	}

	if (m_proxy_status == socks_proxy_resolve)
	{
		m_proxy_status = socks_connect_proxy;
		// ��ʼ�첽���Ӵ���.
		boost::asio::async_connect(sock.lowest_layer(), endpoint_iterator,
			boost::bind(&http_stream::handle_connect_socks<Stream, Handler>,
				this, boost::ref(sock), handler,
				endpoint_iterator, boost::asio::placeholders::error
			)
		);

		return;
	}

	if (m_proxy_status == socks4_resolve_host)
	{
		// ����IP��PORT��Ϣ.
		m_remote_endp = *endpoint_iterator;
		m_remote_endp.port(m_url.port());

		// ����״̬.
		handle_socks_process(sock, handler, 0, err);
	}
}

template <typename Stream, typename Handler>
void http_stream::handle_connect_socks(Stream &sock, Handler handler,
	tcp::resolver::iterator endpoint_iterator, const boost::system::error_code &err)
{
	using namespace avhttp::detail;

	if (err)
	{
		tcp::resolver::iterator end;
		if (endpoint_iterator == end)
		{
			handler(err);
			return;
		}

		// ��������������һ��IP.
		endpoint_iterator++;
		boost::asio::async_connect(sock.lowest_layer(), endpoint_iterator,
			boost::bind(&http_stream::handle_connect_socks<Stream, Handler>,
				this, boost::ref(sock), handler,
				endpoint_iterator, boost::asio::placeholders::error
			)
		);

		return;
	}

	// ���ӳɹ�, ����Э��汾��.
	if (m_proxy.type == proxy_settings::socks5 || m_proxy.type == proxy_settings::socks5_pw)
	{
		// ���Ͱ汾��Ϣ.
		m_proxy_status = socks_send_version;

		m_request.consume(m_request.size());

		std::size_t bytes_to_write = m_proxy.username.empty() ? 3 : 4;
		boost::asio::mutable_buffer b = m_request.prepare(bytes_to_write);
		char *p = boost::asio::buffer_cast<char*>(b);
		write_uint8(5, p);		// SOCKS VERSION 5.
		if (m_proxy.username.empty())
		{
			write_uint8(1, p); // 1 authentication method (no auth)
			write_uint8(0, p); // no authentication
		}
		else
		{
			write_uint8(2, p); // 2 authentication methods
			write_uint8(0, p); // no authentication
			write_uint8(2, p); // username/password
		}

		m_request.commit(bytes_to_write);

		typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
		boost::asio::async_write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write),
			boost::bind(&http_stream::handle_socks_process<Stream, HandlerWrapper>,
				this, boost::ref(sock), HandlerWrapper(handler),
				boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error
			)
		);

		return;
	}

	if (m_proxy.type == proxy_settings::socks4)
	{
		m_proxy_status = socks4_resolve_host;

		// �����첽��ѯԶ��������HOST.
		std::ostringstream port_string;
		port_string << m_url.port();
		tcp::resolver::query query(m_url.host(), port_string.str());

		// ��ʼ�첽��������Ķ˿ں�������.
		typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
		m_resolver.async_resolve(query,
			boost::bind(&http_stream::async_socks_proxy_resolve<Stream, HandlerWrapper>,
				this,
				boost::asio::placeholders::error, boost::asio::placeholders::iterator,
				boost::ref(sock), HandlerWrapper(handler)
			)
		);
	}
}

template <typename Stream, typename Handler>
void http_stream::handle_socks_process(Stream &sock, Handler handler,
	int bytes_transferred, const boost::system::error_code &err)
{
	using namespace avhttp::detail;

	if (err)
	{
		handler(err);
		return;
	}

	switch (m_proxy_status)
	{
	case socks_send_version:	// ��ɰ汾�ŷ���.
		{
			// ����socks����������.
			std::size_t bytes_to_read;
			if (m_proxy.type == proxy_settings::socks5 || m_proxy.type == proxy_settings::socks5_pw)
				bytes_to_read = 10;
			else if (m_proxy.type == proxy_settings::socks4)
				bytes_to_read = 8;

			if (m_proxy.type == proxy_settings::socks4)
			{
				// �޸�״̬.
				m_proxy_status = socks4_response;

				m_response.consume(m_response.size());
				boost::asio::async_read(sock, m_response, boost::asio::transfer_exactly(bytes_to_read),
					boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
						this, boost::ref(sock), handler,
						boost::asio::placeholders::bytes_transferred,
						boost::asio::placeholders::error
					)
				);

				return;
			}

			if (m_proxy.type == proxy_settings::socks5 || m_proxy.type == proxy_settings::socks5_pw)
			{
				m_proxy_status = socks5_response_version;

				// ��ȡ�汾��Ϣ.
				m_response.consume(m_response.size());
				boost::asio::async_read(sock, m_response, boost::asio::transfer_exactly(2),
					boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
						this, boost::ref(sock), handler,
						boost::asio::placeholders::bytes_transferred,
						boost::asio::placeholders::error
					)
				);

				return;
			}
		}
		break;
	case socks4_resolve_host:	// socks4Э��, IP/PORT�Ѿ��õ�, ��ʼ���Ͱ汾��Ϣ.
		{
			m_proxy_status = socks_send_version;

			m_request.consume(m_request.size());
			std::size_t bytes_to_write = 9 + m_proxy.username.size();
			boost::asio::mutable_buffer mb = m_request.prepare(bytes_to_write);
			char *wp = boost::asio::buffer_cast<char*>(mb);

			write_uint8(4, wp); // SOCKS VERSION 4.
			write_uint8(1, wp); // CONNECT command.

			// socks4Э��ֻ����ip��ַ, ��֧������.
			unsigned long ip = m_remote_endp.address().to_v4().to_ulong();
			write_uint16(m_remote_endp.port(), wp);	// port.
			write_uint32(ip, wp);					// ip address.

			// username.
			if (!m_proxy.username.empty())
			{
				std::copy(m_proxy.username.begin(), m_proxy.username.end(), wp);
				wp += m_proxy.username.size();
			}
			// NULL terminator.
			write_uint8(0, wp);

			m_request.commit(bytes_to_write);

			boost::asio::async_write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write),
				boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
					this, boost::ref(sock), handler,
					boost::asio::placeholders::bytes_transferred, boost::asio::placeholders::error
				)
			);

			return;
		}
		break;
	case socks5_send_userinfo:
		{
			m_proxy_status = socks5_auth_status;
			// ��ȡ��֤״̬.
			m_response.consume(m_response.size());
			boost::asio::async_read(sock, m_response, boost::asio::transfer_exactly(2),
				boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
					this, boost::ref(sock), handler,
					boost::asio::placeholders::bytes_transferred,
					boost::asio::placeholders::error
				)
			);
			return;
		}
		break;
	case socks5_connect_request:
		{
			m_proxy_status = socks5_connect_response;

			// ����״̬��Ϣ.
			m_request.consume(m_request.size());
			std::string host = m_url.host();
			std::size_t bytes_to_write = 7 + host.size();
			boost::asio::mutable_buffer mb = m_request.prepare(bytes_to_write);
			char *wp = boost::asio::buffer_cast<char*>(mb);
			// ����socks5��������.
			write_uint8(5, wp); // SOCKS VERSION 5.
			write_uint8(1, wp); // CONNECT command.
			write_uint8(0, wp); // reserved.
			write_uint8(3, wp); // address type.
			BOOST_ASSERT(host.size() <= 255);
			write_uint8(host.size(), wp);				// domainname size.
			std::copy(host.begin(), host.end(),wp);		// domainname.
			wp += host.size();
			write_uint16(m_url.port(), wp);				// port.
			m_request.commit(bytes_to_write);
			boost::asio::async_write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write),
				boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
					this, boost::ref(sock), handler,
					boost::asio::placeholders::bytes_transferred, boost::asio::placeholders::error
				)
			);

			return;
		}
		break;
	case socks5_connect_response:
		{
			m_proxy_status = socks5_result;
			std::size_t bytes_to_read = 10;
			m_response.consume(m_response.size());
			boost::asio::async_read(sock, m_response, boost::asio::transfer_exactly(bytes_to_read),
				boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
					this, boost::ref(sock), handler,
					boost::asio::placeholders::bytes_transferred,
					boost::asio::placeholders::error
				)
			);
		}
		break;
	case socks4_response:	// socks4��������������.
		{
			// ��������������.
			boost::asio::const_buffer cb = m_response.data();
			const char *rp = boost::asio::buffer_cast<const char*>(cb);
			/*int version = */read_uint8(rp);
			int response = read_uint8(rp);

			// 90: request granted.
			// 91: request rejected or failed.
			// 92: request rejected becasue SOCKS server cannot connect to identd on the client.
			// 93: request rejected because the client program and identd report different user-ids.
			if (response == 90)	// access granted.
			{
				m_response.consume(m_response.size());	// û�з�������, ��ʼ�첽��������.

#ifdef AVHTTP_ENABLE_OPENSSL
				if (m_protocol == "https")
				{
					// ��ʼ����.
					m_proxy_status = ssl_handshake;
					ssl_socket* ssl_sock = m_sock.get<ssl_socket>();
					ssl_sock->async_handshake(boost::bind(&http_stream::handle_socks_process<Stream, Handler>, this,
						boost::ref(sock), handler,
						0,
						boost::asio::placeholders::error));
					return;
				}
				else
#endif
				async_request(m_request_opts, handler);
				return;
			}
			else
			{
				boost::system::error_code ec = errc::socks_general_failure;
				switch (response)
				{
				case 91: ec = errc::socks_authentication_error; break;
				case 92: ec = errc::socks_no_identd; break;
				case 93: ec = errc::socks_identd_error; break;
				}
				handler(ec);
				return;
			}
		}
		break;
#ifdef AVHTTP_ENABLE_OPENSSL
	case ssl_handshake:
		{
			async_request(m_request_opts, handler);
		}
		break;
#endif
	case socks5_response_version:
		{
			boost::asio::const_buffer cb = m_response.data();
			const char *rp = boost::asio::buffer_cast<const char*>(cb);
			int version = read_uint8(rp);
			int method = read_uint8(rp);
			if (version != 5)	// �汾������5, ��֧��socks5.
			{
				boost::system::error_code ec = make_error_code(errc::socks_unsupported_version);
				handler(ec);
				return;
			}

			const proxy_settings &s = m_proxy;

			if (method == 2)
			{
				if (s.username.empty())
				{
					boost::system::error_code ec = make_error_code(errc::socks_username_required);
					handler(ec);
					return;
				}

				// start sub-negotiation.
				m_request.consume(m_request.size());
				std::size_t bytes_to_write = m_proxy.username.size() + m_proxy.password.size() + 3;
				boost::asio::mutable_buffer mb = m_request.prepare(bytes_to_write);
				char *wp = boost::asio::buffer_cast<char*>(mb);
				write_uint8(1, wp);
				write_uint8(s.username.size(), wp);
				write_string(s.username, wp);
				write_uint8(s.password.size(), wp);
				write_string(s.password, wp);
				m_request.commit(bytes_to_write);

				// �޸�״̬.
				m_proxy_status = socks5_send_userinfo;

				// �����û�������Ϣ.
				boost::asio::async_write(sock, m_request, boost::asio::transfer_exactly(bytes_to_write),
					boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
						this, boost::ref(sock), handler,
						boost::asio::placeholders::bytes_transferred, boost::asio::placeholders::error
					)
				);

				return;
			}

			if (method == 0)
			{
				m_proxy_status = socks5_connect_request;
				handle_socks_process(sock, handler, 0, err);
				return;
			}
		}
		break;
	case socks5_auth_status:
		{
			boost::asio::const_buffer cb = m_response.data();
			const char *rp = boost::asio::buffer_cast<const char*>(cb);

			int version = read_uint8(rp);
			int status = read_uint8(rp);

			if (version != 1)	// ��֧�ֵİ汾.
			{
				boost::system::error_code ec = make_error_code(errc::socks_unsupported_authentication_version);
				handler(ec);
				return;
			}

			if (status != 0)	// ��֤����.
			{
				boost::system::error_code ec = make_error_code(errc::socks_authentication_error);
				handler(ec);
				return;
			}

			// ����������������.
			m_proxy_status = socks5_connect_request;
			handle_socks_process(sock, handler, 0, err);
		}
		break;
	case socks5_result:
		{
			// ��������������.
			boost::asio::const_buffer cb = m_response.data();
			const char *rp = boost::asio::buffer_cast<const char*>(cb);
			int version = read_uint8(rp);
			int response = read_uint8(rp);

			if (version != 5)
			{
				boost::system::error_code ec = make_error_code(errc::socks_general_failure);
				handler(ec);
				return;
			}

			if (response != 0)
			{
				boost::system::error_code ec = make_error_code(errc::socks_general_failure);
				// �õ�����ϸ�Ĵ�����Ϣ.
				switch (response)
				{
				case 2: ec = boost::asio::error::no_permission; break;
				case 3: ec = boost::asio::error::network_unreachable; break;
				case 4: ec = boost::asio::error::host_unreachable; break;
				case 5: ec = boost::asio::error::connection_refused; break;
				case 6: ec = boost::asio::error::timed_out; break;
				case 7: ec = make_error_code(errc::socks_command_not_supported); break;
				case 8: ec = boost::asio::error::address_family_not_supported; break;
				}
				handler(ec);
				return;
			}

			rp++;	// skip reserved.
			int atyp = read_uint8(rp);	// atyp.

			if (atyp == 1)		// address / port ��ʽ����.
			{
				m_response.consume(m_response.size());

#ifdef AVHTTP_ENABLE_OPENSSL
				if (m_protocol == "https")
				{
					// ��ʼ����.
					m_proxy_status = ssl_handshake;
					ssl_socket* ssl_sock = m_sock.get<ssl_socket>();
					ssl_sock->async_handshake(boost::bind(&http_stream::handle_socks_process<Stream, Handler>, this,
						boost::ref(sock), handler,
						0,
						boost::asio::placeholders::error));
					return;
				}
				else
#endif
				// û�з�������, ��ʼ�첽��������.
				async_request(m_request_opts, handler);

				return;
			}
			else if (atyp == 3)				// domainname ����.
			{
				int len = read_uint8(rp);	// ��ȡdomainname����.
				std::size_t bytes_to_read = len - 3;

				m_proxy_status = socks5_read_domainname;

				m_response.consume(m_response.size());
				boost::asio::async_read(sock, m_response, boost::asio::transfer_exactly(bytes_to_read),
					boost::bind(&http_stream::handle_socks_process<Stream, Handler>,
						this, boost::ref(sock), handler,
						boost::asio::placeholders::bytes_transferred,
						boost::asio::placeholders::error
					)
				);

				return;
			}
			// else if (atyp == 4)	// ipv6 ����, ����ʵ��!
			// {
			//	ec = boost::asio::error::address_family_not_supported;
			//	return;
			// }
			else
			{
				boost::system::error_code ec = boost::asio::error::address_family_not_supported;
				handler(ec);
				return;
			}
		}
		break;
	case socks5_read_domainname:
		{
			m_response.consume(m_response.size());

#ifdef AVHTTP_ENABLE_OPENSSL
			if (m_protocol == "https")
			{
				// ��ʼ����.
				m_proxy_status = ssl_handshake;
				ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
				ssl_sock->async_handshake(boost::bind(&http_stream::handle_socks_process<Stream, Handler>, this,
					boost::ref(sock), handler,
					0,
					boost::asio::placeholders::error));
				return;
			}
			else
#endif
			// û�з�������, ��ʼ�첽��������.
			async_request(m_request_opts, handler);
			return;
		}
		break;
	}
}

// ʵ��CONNECTָ��, ��������Ŀ��Ϊhttps����ʱʹ��.
template <typename Stream, typename Handler>
void http_stream::async_https_proxy_connect(Stream &sock, Handler handler)
{
	// �����첽��ѯproxy������Ϣ.
	std::ostringstream port_string;
	port_string << m_proxy.port;
	tcp::resolver::query query(m_proxy.hostname, port_string.str());

	// ��ʼ�첽��������Ķ˿ں�������.
	typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
	m_resolver.async_resolve(query,
		boost::bind(&http_stream::async_https_proxy_resolve<Stream, HandlerWrapper>,
			this, boost::asio::placeholders::error,
			boost::asio::placeholders::iterator,
			boost::ref(sock),
			HandlerWrapper(handler)
		)
	);
}

template <typename Stream, typename Handler>
void http_stream::async_https_proxy_resolve(const boost::system::error_code &err,
	tcp::resolver::iterator endpoint_iterator, Stream &sock, Handler handler)
{
	if (err)
	{
		handler(err);
		return;
	}
	// ��ʼ�첽���Ӵ���.
	boost::asio::async_connect(sock.lowest_layer(), endpoint_iterator,
		boost::bind(&http_stream::handle_connect_https_proxy<Stream, Handler>,
			this, boost::ref(sock), handler,
			endpoint_iterator, boost::asio::placeholders::error
		)
	);
	return;
}

template <typename Stream, typename Handler>
void http_stream::handle_connect_https_proxy(Stream &sock, Handler handler,
	tcp::resolver::iterator endpoint_iterator, const boost::system::error_code &err)
{
	if (err)
	{
		tcp::resolver::iterator end;
		if (endpoint_iterator == end)
		{
			handler(err);
			return;
		}

		// ��������������һ��IP.
		endpoint_iterator++;
		boost::asio::async_connect(sock.lowest_layer(), endpoint_iterator,
			boost::bind(&http_stream::handle_connect_https_proxy<Stream, Handler>,
				this, boost::ref(sock), handler,
				endpoint_iterator, boost::asio::placeholders::error
			)
		);

		return;
	}

	// ����CONNECT����.
	request_opts opts = m_request_opts;

	// �����������.
	std::string request_method = "CONNECT";

	// ������http/1.1�汾.
	std::string http_version = "HTTP/1.1";

	// ���user_agent.
	std::string user_agent = "avhttp/2.1";
	if (opts.find(http_options::user_agent, user_agent))
		opts.remove(http_options::user_agent);	// ɾ���������ѡ��.

	// �õ�Accept��Ϣ.
	std::string accept = "text/html, application/xhtml+xml, */*";
	if (opts.find(http_options::accept, accept))
		opts.remove(http_options::accept);		// ɾ���������ѡ��.

	// �õ�Host��Ϣ.
	std::string host = m_url.to_string(url::host_component | url::port_component);
	if (opts.find(http_options::host, host))
		opts.remove(http_options::host);		// ɾ���������ѡ��.

	// ���ϸ�ѡ�Http�����ַ�����.
	std::string request_string;
	m_request.consume(m_request.size());
	std::ostream request_stream(&m_request);
	request_stream << request_method << " ";
	request_stream << m_url.host() << ":" << m_url.port();
	request_stream << " " << http_version << "\r\n";
	request_stream << "Host: " << host << "\r\n";
	request_stream << "Accept: " << accept << "\r\n";
	request_stream << "User-Agent: " << user_agent << "\r\n\r\n";

	// �첽��������.
	typedef boost::function<void (boost::system::error_code)> HandlerWrapper;
	boost::asio::async_write(sock, m_request, boost::asio::transfer_exactly(m_request.size()),
		boost::bind(&http_stream::handle_https_proxy_request<Stream, HandlerWrapper>,
			this,
			boost::ref(sock), HandlerWrapper(handler),
			boost::asio::placeholders::error
		)
	);
}

template <typename Stream, typename Handler>
void http_stream::handle_https_proxy_request(Stream &sock, Handler handler,
	const boost::system::error_code &err)
{
	// ��������.
	if (err)
	{
		handler(err);
		return;
	}

	// �첽��ȡHttp status.
	boost::asio::async_read_until(sock, m_response, "\r\n",
		boost::bind(&http_stream::handle_https_proxy_status<Stream, Handler>,
			this,
			boost::ref(sock), handler,
			boost::asio::placeholders::error
		)
	);
}

template <typename Stream, typename Handler>
void http_stream::handle_https_proxy_status(Stream &sock, Handler handler,
	const boost::system::error_code &err)
{
	// ��������.
	if (err)
	{
		handler(err);
		return;
	}

	// ����״̬��.
	// ���http״̬��, version_major��version_minor��httpЭ��İ汾��.
	int version_major = 0;
	int version_minor = 0;
	m_status_code = 0;
	if (!detail::parse_http_status_line(
		std::istreambuf_iterator<char>(&m_response),
		std::istreambuf_iterator<char>(),
		version_major, version_minor, m_status_code))
	{
		handler(avhttp::errc::malformed_status_line);
		return;
	}

	// "continue"��ʾ������Ҫ�����ȴ�����״̬.
	if (m_status_code == avhttp::errc::continue_request)
	{
		boost::asio::async_read_until(sock, m_response, "\r\n",
			boost::bind(&http_stream::handle_https_proxy_status<Stream, Handler>,
				this,
				boost::ref(sock), handler,
				boost::asio::placeholders::error
			)
		);
	}
	else
	{
		// ���ԭ�еķ���ѡ��.
		m_response_opts.clear();

		// ���״̬��.
		m_response_opts.insert("_status_code", boost::str(boost::format("%d") % m_status_code));

		// �첽��ȡ����Http header����.
		boost::asio::async_read_until(sock, m_response, "\r\n\r\n",
			boost::bind(&http_stream::handle_https_proxy_header<Stream, Handler>,
				this,
				boost::ref(sock), handler,
				boost::asio::placeholders::bytes_transferred,
				boost::asio::placeholders::error
			)
		);
	}
}

template <typename Stream, typename Handler>
void http_stream::handle_https_proxy_header(Stream &sock, Handler handler,
	int bytes_transferred, const boost::system::error_code &err)
{
	if (err)
	{
		handler(err);
		return;
	}

	std::string header_string;
	header_string.resize(bytes_transferred);
	m_response.sgetn(&header_string[0], bytes_transferred);

	// ����Http Header.
	if (!detail::parse_http_headers(header_string.begin(), header_string.end(),
		m_content_type, m_content_length, m_location, m_response_opts.option_all()))
	{
		handler(avhttp::errc::malformed_response_headers);
		return;
	}

	boost::system::error_code ec;

	if (m_status_code != avhttp::errc::ok)
	{
		ec = make_error_code(static_cast<avhttp::errc::errc_t>(m_status_code));
		// �ص�֪ͨ.
		handler(ec);
		return;
	}

	// ��ʼ�첽����.
	ssl_socket *ssl_sock = m_sock.get<ssl_socket>();
	ssl_sock->async_handshake(
		boost::bind(&http_stream::handle_https_proxy_handshake<Stream, Handler>,
			this,
			boost::ref(sock),
			handler,
			boost::asio::placeholders::error
		)
	);
	return;
}

template <typename Stream, typename Handler>
void http_stream::handle_https_proxy_handshake(Stream &sock, Handler handler,
	const boost::system::error_code &err)
{
	if (err)
	{
		// �ص�֪ͨ.
		handler(err);
		return;
	}

	// ��ս��ջ�����.
	m_response.consume(m_response.size());

	// �����첽����.
	async_request(m_request_opts, handler);
}

// ʵ��CONNECTָ��, ��������Ŀ��Ϊhttps����ʱʹ��.
template <typename Stream>
void http_stream::https_proxy_connect(Stream &sock, boost::system::error_code &ec)
{
	// ��ʼ�����˿ں�������.
	tcp::resolver resolver(m_io_service);
	std::ostringstream port_string;
	port_string << m_proxy.port;
	tcp::resolver::query query(m_proxy.hostname, port_string.str());
	tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
	tcp::resolver::iterator end;

	// �������ӽ��������Ĵ����������ַ.
	ec = boost::asio::error::host_not_found;
	while (ec && endpoint_iterator != end)
	{
		sock.close(ec);
		sock.connect(*endpoint_iterator++, ec);
	}
	if (ec)
	{
		return;
	}

	// ����CONNECT����.
	request_opts opts = m_request_opts;

	// �����������.
	std::string request_method = "CONNECT";

	// ������http/1.1�汾.
	std::string http_version = "HTTP/1.1";

	// ���user_agent.
	std::string user_agent = "avhttp/2.1";
	if (opts.find(http_options::user_agent, user_agent))
		opts.remove(http_options::user_agent);	// ɾ���������ѡ��.

	// �õ�Accept��Ϣ.
	std::string accept = "text/html, application/xhtml+xml, */*";
	if (opts.find(http_options::accept, accept))
		opts.remove(http_options::accept);		// ɾ���������ѡ��.

	// �õ�Host��Ϣ.
	std::string host = m_url.to_string(url::host_component | url::port_component);
	if (opts.find(http_options::host, host))
		opts.remove(http_options::host);		// ɾ���������ѡ��.

	// ���ϸ�ѡ�Http�����ַ�����.
	std::string request_string;
	m_request.consume(m_request.size());
	std::ostream request_stream(&m_request);
	request_stream << request_method << " ";
	request_stream << m_url.host() << ":" << m_url.port();
	request_stream << " " << http_version << "\r\n";
	request_stream << "Host: " << host << "\r\n";
	request_stream << "Accept: " << accept << "\r\n";
	request_stream << "User-Agent: " << user_agent << "\r\n\r\n";

	// ��������.
	boost::asio::write(sock, m_request, ec);
	if (ec)
	{
		return;
	}

	// ѭ����ȡ.
	for (;;)
	{
		boost::asio::read_until(sock, m_response, "\r\n", ec);
		if (ec)
		{
			return;
		}

		// ���http״̬��, version_major��version_minor��httpЭ��İ汾��.
		int version_major = 0;
		int version_minor = 0;
		m_status_code = 0;
		if (!detail::parse_http_status_line(
			std::istreambuf_iterator<char>(&m_response),
			std::istreambuf_iterator<char>(),
			version_major, version_minor, m_status_code))
		{
			ec = avhttp::errc::malformed_status_line;
			return;
		}

		// ���http״̬���벻��ok���ʾ����.
		if (m_status_code != avhttp::errc::ok)
		{
			ec = make_error_code(static_cast<avhttp::errc::errc_t>(m_status_code));
		}

		// "continue"��ʾ������Ҫ�����ȴ�����״̬.
		if (m_status_code != avhttp::errc::continue_request)
			break;
	} // end for.

	// ���ԭ�еķ���ѡ��.
	m_response_opts.clear();

	// ���״̬��.
	m_response_opts.insert("_status_code", boost::str(boost::format("%d") % m_status_code));

	// ���յ�����Http Header.
	boost::system::error_code read_err;
	std::size_t bytes_transferred = boost::asio::read_until(sock, m_response, "\r\n\r\n", read_err);
	if (read_err)
	{
		// ˵�������˽�����û�еõ�Http header, ���ش�����ļ�ͷ��Ϣ��������eof.
		if (read_err == boost::asio::error::eof)
			ec = avhttp::errc::malformed_response_headers;
		else
			ec = read_err;
		return;
	}

	std::string header_string;
	header_string.resize(bytes_transferred);
	m_response.sgetn(&header_string[0], bytes_transferred);

	// ����Http Header.
	if (!detail::parse_http_headers(header_string.begin(), header_string.end(),
		m_content_type, m_content_length, m_location, m_response_opts.option_all()))
	{
		ec = avhttp::errc::malformed_response_headers;
		return;
	}

	m_response.consume(m_response.size());

	return;
}

template <typename Stream>
void http_stream::request_impl(Stream &sock, request_opts &opt, boost::system::error_code &ec)
{
	// �ж�socket�Ƿ��.
	if (!sock.is_open())
	{
		ec = boost::asio::error::network_reset;
		return;
	}

	// ���浽һ���µ�opts�в���.
	request_opts opts = opt;

	// �õ�urlѡ��.
	std::string new_url;
	if (opts.find(http_options::url, new_url))
		opts.remove(http_options::url);		// ɾ���������ѡ��.

	if (!new_url.empty())
	{
		BOOST_ASSERT(url::from_string(new_url).host() == m_url.host());	// ������ͬһ����.
		m_url = new_url;
	}

	// �õ�request_method.
	std::string request_method = "GET";
	if (opts.find(http_options::request_method, request_method))
		opts.remove(http_options::request_method);	// ɾ���������ѡ��.

	// �õ�http�汾��Ϣ.
	std::string http_version = "HTTP/1.1";
	if (opts.find(http_options::http_version, http_version))
		opts.remove(http_options::http_version);	// ɾ���������ѡ��.

	// �õ�Host��Ϣ.
	std::string host = m_url.to_string(url::host_component | url::port_component);
	if (opts.find(http_options::host, host))
		opts.remove(http_options::host);	// ɾ���������ѡ��.

	// �õ�Accept��Ϣ.
	std::string accept = "text/html, application/xhtml+xml, */*";
	if (opts.find(http_options::accept, accept))
		opts.remove(http_options::accept);	// ɾ���������ѡ��.

	// ���user_agent.
	std::string user_agent = "avhttp/2.1";
	if (opts.find(http_options::user_agent, user_agent))
		opts.remove(http_options::user_agent);	// ɾ���������ѡ��.

	// Ĭ�����close.
	std::string connection = "close";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
	{
		if (opts.find(http_options::proxy_connection, connection))
			opts.remove(http_options::proxy_connection);		// ɾ���������ѡ��.
	}
	else
	{
		if (opts.find(http_options::connection, connection))
			opts.remove(http_options::connection);		// ɾ���������ѡ��.
	}

	// �Ƿ����bodyѡ��.
	std::string body;
	if (opts.find(http_options::request_body, body))
		opts.remove(http_options::request_body);	// ɾ���������ѡ��.

	// ѭ����������ѡ��.
	std::string other_option_string;
	request_opts::option_item_list &list = opts.option_all();
	for (request_opts::option_item_list::iterator val = list.begin(); val != list.end(); val++)
	{
		other_option_string += (val->first + ": " + val->second + "\r\n");
	}

	// ���ϸ�ѡ�Http�����ַ�����.
	std::string request_string;
	m_request.consume(m_request.size());
	std::ostream request_stream(&m_request);
	request_stream << request_method << " ";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
		request_stream << m_url.to_string().c_str();
	else
		request_stream << m_url.to_string(url::path_component | url::query_component);
	request_stream << " " << http_version << "\r\n";
	request_stream << "Host: " << host << "\r\n";
	request_stream << "Accept: " << accept << "\r\n";
	request_stream << "User-Agent: " << user_agent << "\r\n";
	if ((m_proxy.type == proxy_settings::http_pw || m_proxy.type == proxy_settings::http)
		&& m_protocol != "https")
		request_stream << "Proxy-Connection: " << connection << "\r\n";
	else
		request_stream << "Connection: " << connection << "\r\n";
	request_stream << other_option_string << "\r\n";
	if (!body.empty())
	{
		request_stream << body;
	}

	// ��������.
	boost::asio::write(sock, m_request, ec);
	if (ec)
	{
		return;
	}

	// ѭ����ȡ.
	for (;;)
	{
		boost::asio::read_until(sock, m_response, "\r\n", ec);
		if (ec)
		{
			return;
		}

		// ���Ƶ��µ�streambuf�д�������http״̬, �������http״̬��, ��ô������m_response�е�����,
		// ����Ҫ��Ϊ�˼��ݷǱ�׼http������ֱ����ͻ��˷����ļ�����Ҫ, ������Ȼ��Ҫ��malformed_status_line
		// ֪ͨ�û�, ����m_response�е�������δ���, ���û��Լ������Ƿ��ȡ.
		boost::asio::streambuf tempbuf;
		int response_size = m_response.size();
		boost::asio::streambuf::const_buffers_type::const_iterator begin(m_response.data().begin());
		const char* ptr = boost::asio::buffer_cast<const char*>(*begin);
		std::ostream tempbuf_stream(&tempbuf);
		tempbuf_stream.write(ptr, response_size);

		// ���http״̬��, version_major��version_minor��httpЭ��İ汾��.
		int version_major = 0;
		int version_minor = 0;
		m_status_code = 0;
		if (!detail::parse_http_status_line(
			std::istreambuf_iterator<char>(&tempbuf),
			std::istreambuf_iterator<char>(),
			version_major, version_minor, m_status_code))
		{
			ec = avhttp::errc::malformed_status_line;
			return;
		}

		// �����״̬����ռ�õ��ֽ���.
		m_response.consume(response_size - tempbuf.size());

		// ���http״̬���벻��ok��partial_content, ����status_code����һ��http_code, ����
		// ��Ҫ�ж�http_code�ǲ���302����ת, �����, �򽫽�����ת�߼�; �����http�����˴���
		// , ��ֱ�ӷ������״̬�����.
		if (m_status_code != avhttp::errc::ok &&
			m_status_code != avhttp::errc::partial_content)
		{
			ec = make_error_code(static_cast<avhttp::errc::errc_t>(m_status_code));
		}

		// "continue"��ʾ������Ҫ�����ȴ�����״̬.
		if (m_status_code != avhttp::errc::continue_request)
			break;
	} // end for.

	// ���ԭ�еķ���ѡ��.
	m_response_opts.clear();
	// ���״̬��.
	m_response_opts.insert("_status_code", boost::str(boost::format("%d") % m_status_code));

	// ���յ�����Http Header.
	boost::system::error_code read_err;
	std::size_t bytes_transferred = boost::asio::read_until(sock, m_response, "\r\n\r\n", read_err);
	if (read_err)
	{
		// ˵�������˽�����û�еõ�Http header, ���ش�����ļ�ͷ��Ϣ��������eof.
		if (read_err == boost::asio::error::eof)
			ec = avhttp::errc::malformed_response_headers;
		else
			ec = read_err;
		return;
	}

	std::string header_string;
	header_string.resize(bytes_transferred);
	m_response.sgetn(&header_string[0], bytes_transferred);

	// ����Http Header.
	if (!detail::parse_http_headers(header_string.begin(), header_string.end(),
		m_content_type, m_content_length, m_location, m_response_opts.option_all()))
	{
		ec = avhttp::errc::malformed_response_headers;
		return;
	}

	// �����Ƿ�������gzѹ��.
	std::string encoding = m_response_opts.find(http_options::content_encoding);
#ifdef AVHTTP_ENABLE_ZLIB
	if (encoding == "gzip" || encoding == "x-gzip")
		m_is_gzip = true;
#endif
	encoding = m_response_opts.find(http_options::transfer_encoding);
	if (encoding == "chunked")
		m_is_chunked = true;
}


#ifdef AVHTTP_ENABLE_OPENSSL

// Return true is STRING (case-insensitively) matches PATTERN, false
// otherwise.  The recognized wildcard character is "*", which matches
// any character in STRING except ".".  Any number of the "*" wildcard
// may be present in the pattern.
//
// This is used to match of hosts as indicated in rfc2818: "Names may
// contain the wildcard character * which is considered to match any
// single domain name component or component fragment. E.g., *.a.com
// matches foo.a.com but not bar.foo.a.com. f*.com matches foo.com but
// not bar.com [or foo.bar.com]."
//
// If the pattern contain no wildcards, pattern_match(a, b) is
// equivalent to !strcasecmp(a, b).

#define ASTERISK_EXCLUDES_DOT   /* mandated by rfc2818 */

inline bool http_stream::pattern_match(const char *pattern, const char *string)
{
	const char *p = pattern, *n = string;
	char c;
	for (; (c = std::tolower(*p++)) != '\0'; n++)
	{
		if (c == '*')
		{
			for (c = tolower(*p); c == '*' || c == '.'; c = std::tolower(*++p))
				;
			for (; *n != '\0'; n++)
			{
				if (std::tolower(*n) == c && pattern_match(p, n))
					return true;
#ifdef ASTERISK_EXCLUDES_DOT			/* mandated by rfc2818 */
				else if (*n == '.')
				{
					if (std::strcmp(n + 1, p) == 0)
						return true;
					else
						return false;
				}
#endif
			}
			return c == '\0';
		}
		else
		{
			if (c != std::tolower(*n))
				return false;
		}
	}
	return *n == '\0';
}

#undef ASTERISK_EXCLUDES_DOT

inline bool http_stream::certificate_matches_host(X509 *cert, const std::string &host)
{
	// Try converting host name to an address. If it is an address then we need
	// to look for an IP address in the certificate rather than a host name.
	boost::system::error_code ec;
	boost::asio::ip::address address
		= boost::asio::ip::address::from_string(host, ec);
	bool is_address = !ec;

	// Go through the alternate names in the certificate looking for DNS or IPADD
	// entries.
	GENERAL_NAMES* gens = static_cast<GENERAL_NAMES*>(
		X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0));
	for (int i = 0; i < sk_GENERAL_NAME_num(gens); ++i)
	{
		GENERAL_NAME* gen = sk_GENERAL_NAME_value(gens, i);
		if (gen->type == GEN_DNS && !is_address)
		{
			ASN1_IA5STRING* domain = gen->d.dNSName;
			if (domain->type == V_ASN1_IA5STRING
				&& domain->data && domain->length)
			{
				unsigned char *name_in_utf8 = NULL;
				if (0 <= ASN1_STRING_to_UTF8 (&name_in_utf8, gen->d.dNSName))
				{
					if (pattern_match(reinterpret_cast<const char*>(name_in_utf8), host.c_str())
						&& std::strlen(reinterpret_cast<const char*>(name_in_utf8))
						== ASN1_STRING_length(gen->d.dNSName))
					{
						OPENSSL_free(name_in_utf8);
						break;
					}
					OPENSSL_free(name_in_utf8);
				}
			}
		}
		else if (gen->type == GEN_IPADD && is_address)
		{
			ASN1_OCTET_STRING* ip_address = gen->d.iPAddress;
			if (ip_address->type == V_ASN1_OCTET_STRING && ip_address->data)
			{
				if (address.is_v4() && ip_address->length == 4)
				{
					boost::asio::ip::address_v4::bytes_type address_bytes
						= address.to_v4().to_bytes();
					if (std::memcmp(address_bytes.data(), ip_address->data, 4) == 0)
						return true;
				}
				else if (address.is_v6() && ip_address->length == 16)
				{
					boost::asio::ip::address_v6::bytes_type address_bytes
						= address.to_v6().to_bytes();
					if (std::memcmp(address_bytes.data(), ip_address->data, 16) == 0)
						return true;
				}
			}
		}
	}

	// No match in the alternate names, so try the common names.
	X509_NAME* name = X509_get_subject_name(cert);
	int i = -1;
	while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0)
	{
		X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
		ASN1_STRING* domain = X509_NAME_ENTRY_get_data(name_entry);
		if (domain->data && domain->length)
		{
			const char* cert_host = reinterpret_cast<const char*>(domain->data);
			if (pattern_match(cert_host, host.c_str()))
				return true;
		}
	}

	return false;
}
#endif // AVHTTP_ENABLE_OPENSSL

}

#endif // __HTTP_STREAM_IPP__
