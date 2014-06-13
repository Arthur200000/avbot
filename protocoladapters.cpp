
/************************************************************************
  protocol adapters, �� libwebqq/libirc/libxmpp 
  Ū�ɼ��� avbot_accounts �ӿ�
  Ŀǰ��������ֻ����ʱ�ģ����ջ���Ҫ���⼸����ȫ��д��
 ************************************************************************/
#include <boost/system/error_code.hpp>
#include <boost/function.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <string>
#include <libirc/irc.hpp>

std::string preamble_qq_fmt, preamble_irc_fmt, preamble_xmpp_fmt;

static std::string	preamble_formater(std::string preamble_irc_fmt, irc::irc_msg pmsg)
{
	// ��ʽ������, ŶҮ.
	// ��ȡ��ʽ�������ַ���.
	std::string preamble = preamble_irc_fmt;

	// ֧�ֵĸ�ʽ�������� %u UID,  %q QQ��, %n �ǳ�,  %c Ⱥ��Ƭ %a �Զ� %r irc ����.
	// Ĭ��Ϊ qq(%a) ˵:.
	boost::replace_all(preamble, "%a", pmsg.whom);
	boost::replace_all(preamble, "%r", pmsg.from);
	boost::replace_all(preamble, "%n", pmsg.whom);
	return preamble;
}

class avwebqq{};

class avxmpp{};

class avirc
{
	irc::client m_irc;
public:
	template<typename Handler>
	void async_login(Handler handler)
	{
		// ִ���첽��¼
		boost::system::error_code ec;
		// ��װ�Ѿ���¼��
		handler(ec);
	}

	template<typename Handler>
	void async_recv_message(Handler handler)
	{
		// �� irc ������Ϣ
		m_handlers.push_back(handler);

		m_irc.on_privmsg_message(boost::bind(&avirc, callback_on_irc_message, this, _1));
	}

	template<typename Handler>
	void async_join_group(std::string groupname, Handler handler)
	{
		boost::system::error_code ec;
		m_irc.join(groupname);
		handler(ec);
	}

	void callback_on_irc_message(irc::irc_msg pMsg)
	{
		using boost::property_tree::ptree;
		// formate irc message to JSON and call on_message

		ptree message;
		message.put("protocol", "irc");
		message.put("room", pMsg.from.substr(1));
		message.put("who.nick", pMsg.whom);
//		message.put("channel", get_channel_name(std::string("irc:") + pMsg.from.substr(1)));
		message.put("preamble", preamble_formater(preamble_irc_fmt, pMsg));

		ptree textmsg;
		textmsg.add("text", pMsg.msg);
		message.add_child("message", textmsg);

		if (!m_handlers.empty())
		{
			boost::system::error_code ec;
			m_handlers.front()(ec, message);
			m_handlers.erase(m_handlers.begin());
		}
	}
	std::vector<
		boost::function<void(boost::system::error_code, boost::property_tree::ptree)>
	>m_handlers;
};
