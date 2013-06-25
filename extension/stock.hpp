/*
 * Copyright (C) 2013  microcai <microcai@fedoraproject.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#pragma once

#ifndef __STOCK_HPP__
#define __STOCK_HPP__

#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>

#include <avhttp.hpp>

#include "extension.hpp"
#include "httpagent.hpp"

// ��������ת��.
#define TYPE_CONVERT(val, type) do { \
	try { val = boost::lexical_cast<type> (str); } \
	catch (...) { val = 0.0f; } } while (false)

namespace stock {

// ���ɵ�ǰ����.
typedef struct stock_data
{
    std::string stock_id;	// ��Ʊ����.
    std::string stock_name; // ��Ʊ����.
    int stock_state;		// �ǻ��ǵ�, ��Ϊ1, ��Ϊ-1, ƽΪ0.
    double current_price;	// ��ǰ��.
    double head_price[10];  // ���������̿�, 5������, 5������.
    double head_number[10]; // ���������̿�����, ͬ��.
    double outamt;	// ����.
    double inamt;	// ����.
    double before_close_price;	// ������.
    double current_open_price;	// ����.
    double best_high_price;		// ��߼�.
    double best_low_price;	// ��ͼ�.
    double amplitude;	    // �񡡷�.
    boost::int64_t volume;	// �ɽ���.
    boost::int64_t amount;	// �ɽ���.
    double total_market_value;  // ����ֵ.
    double pearnings;	        // ��ӯ��.
    double rising_limit;	    // ��ͣ��.
    double falling_limit;       // ��ͣ��.
    double vol_ratio;	// ������.
    double bs_ratio;	// ί����.
    double bs_diff;		// ί����.
    double pnetasset;	// �о���.

    stock_data()
        : stock_id("")
        , stock_name("")
        , stock_state(0)
        , current_price(0.0f)
        , outamt(0.0f)
        , inamt(0.0f)
        , before_close_price(0.0f)
        , best_high_price(0.0f)
        , best_low_price(0.0f)
        , amplitude(0.0f)
        , volume(0)
        , amount(0)
        , total_market_value(0.0f)
        , pearnings(0.0f)
        , rising_limit(0.0f)
        , falling_limit(0.0f)
        , vol_ratio(0.0f)
        , bs_ratio(0.0f)
        , bs_diff(0.0f)
        , pnetasset(0.0f)
    {
        memset(head_price, 0, sizeof(head_price));
        memset(head_number, 0, sizeof(head_number));
    }

} stock_data;

// ���̵�ǰ����.
typedef struct stock_public
{
    std::string stock_name;	    // ��֤����֤.
    std::string stock_id;	    // id.
    double current_price;	    // ��ǰ��.
    double before_close_price;	// ������.
    double current_open_price;  // ����.
    double best_high_price;		// ��߼�.
    double best_low_price;	    // ��ͼ�.
    double amplitude;	        // �񡡷�.
    boost::int64_t turnover;	// �ɽ���.
    boost::int64_t business;	// �ɽ���.
    double rise;	// �� ��.
    double fair;	// �� ƽ.
    double fell;	// �� ��.

    stock_public()
        : stock_name("")
        , stock_id("")
        , current_price(0.0f)
        , before_close_price(0.0f)
        , current_open_price(0.0f)
        , best_high_price(0.0f)
        , best_low_price(0.0f)
        , amplitude(0.0f)
        , turnover(0)
        , business(0)
        , rise(0.0f)
        , fair(0.0f)
        , fell(0.0f)
    {}

} stock_public;

// ������������.
bool analysis_stock_data(std::string &data, stock_data &sd)
{
	boost::regex ex;
	boost::smatch what;
	std::string s(data);
	std::string::const_iterator start, end;
	int count = 0;

	start = s.begin();
	end = s.end();
	ex.assign("([^,;\"\']+)");

	while (boost::regex_search(start, end, what, ex, boost::match_default)) {
		if (what[1].first == what[1].second)
			break;
		int size = what.size();
		std::string str;
		for (int i = 1; i < size; i++) {
			str = std::string(what[i]);
			if (count == 1) {				// NAME.
				sd.stock_name = boost::trim_copy(str);
			} else if (count == 2) {		// ��.
				TYPE_CONVERT(sd.current_open_price, double);
			} else if (count == 3) {		// ��.
				TYPE_CONVERT(sd.before_close_price, double);
			} else if (count == 4) {		// ��.
				TYPE_CONVERT(sd.current_price, double);
			} else if (count == 5) {		// ��.
				TYPE_CONVERT(sd.best_high_price, double);
			} else if (count == 6) {		// ��.
				TYPE_CONVERT(sd.best_low_price, double);
			} else if (count == 9) {		// ��.
				TYPE_CONVERT(sd.volume, boost::int64_t);
			} else if (count == 10) {		// ��.
				TYPE_CONVERT(sd.amount, boost::int64_t);
			} else if (count == 11) {		// m1...m5
				TYPE_CONVERT(sd.head_number[0], double);
			} else if (count == 12) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[0], double);
			} else if (count == 13) {		// m1...m5
				TYPE_CONVERT(sd.head_number[1], double);
			} else if (count == 14) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[1], double);
			} else if (count == 15) {		// m1...m5
				TYPE_CONVERT(sd.head_number[2], double);
			} else if (count == 16) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[2], double);
			} else if (count == 17) {		// m1...m5
				TYPE_CONVERT(sd.head_number[3], double);
			} else if (count == 18) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[3], double);
			} else if (count == 19) {		// m1...m5
				TYPE_CONVERT(sd.head_number[4], double);
			} else if (count == 20) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[4], double);
			} else if (count == 21) {		// m1...m5
				TYPE_CONVERT(sd.head_number[5], double);
			} else if (count == 22) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[5], double);
			} else if (count == 23) {		// m1...m5
				TYPE_CONVERT(sd.head_number[6], double);
			} else if (count == 24) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[6], double);
			} else if (count == 25) {		// m1...m5
				TYPE_CONVERT(sd.head_number[7], double);
			} else if (count == 26) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[7], double);
			} else if (count == 27) {		// m1...m5
				TYPE_CONVERT(sd.head_number[8], double);
			} else if (count == 28) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[8], double);
			} else if (count == 29) {		// m1...m5
				TYPE_CONVERT(sd.head_number[9], double);
			} else if (count == 30) {		// m1p...m5p
				TYPE_CONVERT(sd.head_price[9], double);
			}
		}
		start = what[0].second;
		count++;
	}

	if (count == 0)
		return false;

	return true;
}

// ������������.
bool analysis_stock_data_public(std::string &data, stock_public &sp)
{
	boost::regex ex;
	boost::smatch what;
	std::string s(data);
	std::string::const_iterator start, end;
	int count = 0;

	start = s.begin();
	end = s.end();
	ex.assign("([^,;\"\']+)");

	while (boost::regex_search(start, end, what, ex, boost::match_default)) {
		if (what[1].first == what[1].second)
			break;
		int size = what.size();
		std::string str, id;
		for (int i = 1; i < size; i++) {
			str = std::string(what[i]);
			if (count == 1) {				// NAME.
				sp.stock_name = boost::trim_copy(str);
			} else if (count == 2) {		// ��.
				TYPE_CONVERT(sp.current_open_price, double);
			} else if (count == 3) {		// ��.
				TYPE_CONVERT(sp.before_close_price, double);
			} else if (count == 4) {		// ��.
				TYPE_CONVERT(sp.current_price, double);
			} else if (count == 5) {		// ��.
				TYPE_CONVERT(sp.best_high_price, double);
			} else if (count == 6) {		// ��.
				TYPE_CONVERT(sp.best_low_price, double);
			} else if (count == 9) {		// ��.
				TYPE_CONVERT(sp.turnover, boost::int64_t);
			} else if (count == 10) {		// ��.
				TYPE_CONVERT(sp.business, boost::int64_t);
			}
		}
		start = what[0].second;
		count++;
	}

	if (count == 0)
		return false;

	return true;
}

// �� http://hq.sinajs.cn/?list=sh000001 ��ѯA��ָ��.
template<class MsgSender>
struct stock_fetcher_op
{
	stock_fetcher_op(boost::asio::io_service &io, MsgSender s, std::string q)
	  : m_io_service(io)
	  , m_sender(s)
	  , m_stream(new avhttp::http_stream(io))
	  , m_query(q)
	{
		boost::trim(m_query);
		if (m_query == "��ָ֤��" && m_query == "����" && m_query == "") {
			m_query = "000001";
		} else {
			// ����Ʊ�����Ƿ��������ַ���, �������, �������֧�ֵĲ�ѯ.
			for (std::string::iterator i = m_query.begin();
				i != m_query.end(); i++) {
				if (*i >= '0' && *i <= '9') {
					continue;
				} else {
					m_sender(std::string(m_query + " avbot�ݲ�֧�ָù�Ʊ��ѯ"));
					return;
				}
			}
		}

		// OK, ��ʼ��ѯ��Ʊ.
		std::string url = "http://hq.sinajs.cn/?list=sh" + m_query;
		async_http_download(m_stream, url, *this);
	}

	void operator()(boost::system::error_code ec, read_streamptr stream, boost::asio::streambuf &buf)
	{
		if (!ec || ec == boost::asio::error::eof) {

			std::string jscript;
			jscript.resize(buf.size());
			buf.sgetn(&jscript[0], buf.size());

			if (m_query == "000001") {
				stock_public sh;
				if (analysis_stock_data_public(jscript, sh)) {
					std::string msg = boost::str(boost::format("%s : %0.2f ���̼�: %0.2f")
						% sh.stock_name % sh.current_price % sh.current_open_price);
					m_sender(msg);
				}
			} else {
				stock_data sd;
				if (analysis_stock_data(jscript, sd)) {
					std::string msg = boost::str(boost::format("%s : %0.2f ���̼�: %0.2f")
						% sd.stock_name % sd.current_price % sd.current_open_price);
					m_sender(msg);
				}
			}
		}
	}

	boost::asio::io_service & m_io_service;
	MsgSender m_sender;
	boost::shared_ptr<avhttp::http_stream> m_stream;
	std::string m_query;
};

template<class MsgSender>
void stock_fetcher(boost::asio::io_service & io_service, MsgSender sender, std::string query)
{
	stock_fetcher_op<MsgSender>(io_service, sender, query);
}

} // namespace stock


class stockprice : avbotextension
{
private:

public:
	template<class MsgSender>
	stockprice(boost::asio::io_service &io, MsgSender sender, std::string channel_name)
	  : avbotextension(io, sender, channel_name)
	{}

	void operator()(const boost::system::error_code &error);
	void operator()(boost::property_tree::ptree msg)
	{
		if(msg.get<std::string>("channel") != m_channel_name)
			return;

		std::string textmsg = boost::trim_copy(msg.get<std::string>("message.text"));

		boost::cmatch what;
		if (boost::regex_search(textmsg.c_str(), what, boost::regex(".qqbot ��Ʊ(.*)"))) {
			stock::stock_fetcher(io_service, m_sender, std::string(what[1]));
		}
	}
};

#endif // __STOCK_HPP__
