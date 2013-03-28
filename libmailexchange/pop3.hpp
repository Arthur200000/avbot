
#pragma once


#include <boost/signal.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include "boost/coro/yield.hpp"

#include "boost/timedcall.hpp"
#include "avproxy.hpp"

#include "internet_mail_format.hpp"

class pop3 : boost::coro::coroutine {
public:
	typedef void result_type;
	typedef boost::signal< void (mailcontent thismail) > on_gotmail_signal;
public:
	pop3(::boost::asio::io_service & _io_service, std::string user, std::string passwd, std::string _mailserver="");

	void operator()(const boost::system::error_code & ec, std::size_t length =0)
	{
		using namespace boost::asio;

		ip::tcp::endpoint endpoint;
		std::string		status;
		std::string		maillength;
		std::istream	inbuffer ( m_streambuf.get() );
		std::string		msg;

		reenter ( this ) {
	restart:
			m_socket.reset( new ip::tcp::socket(io_service) );

			do {
	#ifndef DEBUG
				// 延时 60s
				_yield ::boost::delayedcallsec( io_service, 60, boost::bind(*this, ec, 0) );
	#endif

				// dns 解析并连接.
				_yield avproxy::async_proxy_connect(
					avproxy::autoproxychain(*m_socket, ip::tcp::resolver::query(m_mailserver, "110")),
					*this);

				// 失败了延时 10s
				if ( ec )
					_yield ::boost::delayedcallsec ( io_service, 10, boost::bind(*this, ec, 0) );
			} while ( ec ); // 尝试到连接成功为止!

			// 好了，连接上了.
			m_streambuf.reset ( new streambuf );
			// "+OK QQMail POP3 Server v1.0 Service Ready(QQMail v2.0)"
			_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
			inbuffer >> status;

			if ( status != "+OK" ) {
				// 失败，重试.
				goto restart;
			}

			// 发送用户名.
			_yield m_socket->async_write_some ( buffer ( std::string ( "user " ) + m_mailaddr + "\n" ), *this );
			if(ec) goto restart;
			// 接受返回状态.
			m_streambuf.reset ( new streambuf );
			_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
			inbuffer >> status;

			// 解析是不是　OK.
			if ( status != "+OK" ) {
				// 失败，重试.
				goto restart;
			}

			// 发送密码.
			_yield m_socket->async_write_some ( buffer ( std::string ( "pass " ) + m_passwd + "\n" ), *this );
			// 接受返回状态.
			m_streambuf.reset ( new streambuf );
			_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
			inbuffer >> status;

			// 解析是不是　OK.
			if ( status != "+OK" ) {
				// 失败，重试.
				goto restart;
			}

			// 完成登录. 开始接收邮件.

			// 发送　list 命令.
			_yield m_socket->async_write_some ( buffer ( std::string ( "list\n" ) ), *this );
			// 接受返回的邮件.
			m_streambuf.reset ( new streambuf );
			_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
			inbuffer >> status;

			// 解析是不是　OK.
			if ( status != "+OK" ) {
				// 失败，重试.
				goto restart;
			}

			// 开始进入循环处理邮件.
			maillist.clear();
			_yield	m_socket->async_read_some ( m_streambuf->prepare ( 8192 ), *this );
			m_streambuf->commit ( length );

			while ( status != "." ) {
				maillength.clear();
				status.clear();
				inbuffer >> status;
				inbuffer >> maillength;

				// 把邮件的编号push到容器里.
				if ( maillength.length() )
					maillist.push_back ( status );

				if ( inbuffer.eof() && status != "." )
					_yield	m_socket->async_read_some ( m_streambuf->prepare ( 8192 ), *this );
			}

			// 获取邮件.
			while ( !maillist.empty() ) {
				// 发送　retr #number 命令.
				msg = boost::str ( boost::format ( "retr %s\r\n" ) %  maillist[0] );
				_yield m_socket->async_write_some ( buffer ( msg ), *this );
				// 获得　+OK
				m_streambuf.reset ( new streambuf );
				_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
				inbuffer >> status;

				// 解析是不是　OK.
				if ( status != "+OK" ) {
					// 失败，重试.
					goto restart;
				}

				// 获取邮件内容，邮件一单行的 . 结束.
				_yield	async_read_until ( *m_socket, *m_streambuf, "\r\n.\r\n", *this );
				// 然后将邮件内容给处理.
				_yield process_mail ( inbuffer ,  boost::bind(*this, ec, 0));
	#ifndef DEBUG
				// 删除邮件啦.
				msg = boost::str ( boost::format ( "dele %s\r\n" ) %  maillist[0] );
				_yield m_socket->async_write_some ( buffer ( msg ), *this );

				// 获得　+OK
				m_streambuf.reset ( new streambuf );
				_yield	async_read_until ( *m_socket, *m_streambuf, "\n", *this );
				inbuffer >> status;

				// 解析是不是　OK.
				if ( status != "+OK" ) {
					// 失败，但是并不是啥大问题.
					std::cout << "deleting mail failed" << std::endl;
					// but 如果是连接出问题那还是要重启的.
					if(ec) goto restart;
				}
	# endif
				maillist.erase ( maillist.begin() );

			}

			// 处理完毕.
			_yield async_write ( *m_socket, buffer ( "quit\n" ), *this );
			_yield ::boost::delayedcallsec ( io_service, 1, boost::bind ( *this, ec, 0 ) );
			if(m_socket->is_open())
			{
				try{
					m_socket->shutdown ( ip::tcp::socket::shutdown_both );
					m_socket.reset();
				}catch (const boost::system::system_error & ec){
					m_socket.reset();
				}
			}
			std::cout << "邮件处理完毕" << std::endl;
			_yield ::boost::delayedcallsec ( io_service, 30, boost::bind ( *this, ec, 0 ) );
			goto restart;
		}
	}

	void connect_gotmail(const on_gotmail_signal::slot_type& slot)
	{
		m_sig_gotmail->connect(slot);
	}
private:
	template<class Handler>
	void process_mail(std::istream &mail, Handler handler);
private:
	::boost::asio::io_service & io_service;

	std::string m_mailaddr,m_passwd,m_mailserver;
	// 必须是可拷贝的，所以只能用共享指针.
	boost::shared_ptr<boost::asio::ip::tcp::socket>	m_socket;
	boost::shared_ptr<boost::asio::streambuf>	m_streambuf;
	boost::shared_ptr<on_gotmail_signal>		m_sig_gotmail;
	std::vector<std::string>	maillist;
};
