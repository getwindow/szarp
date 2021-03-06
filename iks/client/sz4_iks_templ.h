#include <boost/fusion/include/make_map.hpp>
#include <boost/fusion/sequence/intrinsic/at_key.hpp>

#include "conversion.h"
#include "sz4/defs.h"
#include "data/probe_type.h"
#include "locations/error_codes.h"


namespace sz4 {

namespace bs = boost::system;
namespace bsec = boost::system::errc;
namespace ie = iks_client_error;

const auto value_type_2_name = boost::fusion::make_map<short , int , float , double>("short", "int", "float", "double");
const auto time_type_2_name  = boost::fusion::make_map<second_time_t, nanosecond_time_t>("second_t", "nanosecond_t");

template<class T> void iks::search_data(param_info param, std::string dir, T start, T end, SZARP_PROBE_TYPE probe_type, std::function<void(const boost::system::error_code&, const T&) > cb) {
	auto client = connection_for_base(param.prefix());
	if (!client) {
		cb(make_error_code(ie::not_connected_to_peer), T());
		return;
	}

	std::ostringstream ss;
	ss << "\"" << SC::S2U(param.name()) << "\" "
		<< ProbeType(probe_type).to_string() << " "
		<< dir << " "
		<< boost::fusion::at_key<T>(time_type_2_name) << " "
		<< start << " " << end;

	client->send_command("search_data", ss.str(), [cb] (const bs::error_code& ec, const std::string& status, std::string& data) {
		if (ec) {
			cb(ec, T());
			return IksCmdStatus::cmd_done;
		}

		if (status != "k") {
			cb(make_iks_error_code(data), T());
			return IksCmdStatus::cmd_done;
		}

		T result;
		if (std::istringstream(data) >> result)
			cb(make_error_code(bsec::success), result);
		else
			cb(make_error_code(ie::invalid_server_response), result);

		return IksCmdStatus::cmd_done;
	});
}

template<class V, class T> void iks::_get_weighted_sum(param_info param, T start, T end, SZARP_PROBE_TYPE probe_type, std::function<void(const boost::system::error_code&, const std::vector< weighted_sum<V, T> >&) > cb) {
	typedef std::vector< weighted_sum<V , T> > result_t;

	auto client = connection_for_base(param.prefix());
	if (!client) {
		cb(make_error_code(ie::not_connected_to_peer), result_t());
		return;
	}

	std::ostringstream ss;
	ss << "\"" << SC::S2U(param.name()) << "\" "
		<< ProbeType(probe_type).to_string() << " "
		<< boost::fusion::at_key<V>(value_type_2_name) << " "
		<< boost::fusion::at_key<T>(time_type_2_name) << " "
		<< start << " " << end;
	  
	client->send_command("get_data", ss.str(), [cb] (const bs::error_code& ec, const std::string& status, std::string& data) {
		if (ec) {
			cb(ec, result_t());
			return IksCmdStatus::cmd_done;
		}

		if (status != "k") {
			cb(make_iks_error_code(data), result_t());
			return IksCmdStatus::cmd_done;
		}

		class _wsum : public weighted_sum<V , T> {
		public:
			typedef weighted_sum<V , T> parent_type;
			typename parent_type::sum_type& _sum() { return this->m_wsum; }
			typename parent_type::time_diff_type& _weight() { return this->m_weight; }
			typename parent_type::time_diff_type& _no_data_weight() { return this->m_no_data_weight; }
			bool& _fixed() { return this->m_fixed; }
		};

		std::istringstream ss(data);
		_wsum wsum;
		result_t response;

		while (ss >> wsum._sum() >> wsum._weight() >> wsum._no_data_weight() >> wsum._fixed())
			response.push_back(wsum);

		if (ss.eof())
			cb(make_error_code(bsec::success), response);
		else
			cb(make_error_code(ie::invalid_server_response), result_t());

		return IksCmdStatus::cmd_done;
	});
	
}

template<class V, class T> void iks::get_weighted_sum(const param_info& param ,const T& start ,const T& end, SZARP_PROBE_TYPE probe_type, std::function< void( const boost::system::error_code& , const std::vector< weighted_sum<V , T> >& ) > cb) {
	m_io->post(std::bind(&iks::_get_weighted_sum<V,T>, shared_from_this(), param, start, end, probe_type, cb));
}

template<class T> void iks::search_data_right(const param_info& param, const T& start, const T& end, SZARP_PROBE_TYPE probe_type, std::function<void(const boost::system::error_code&, const T&) > cb) {
	m_io->post(
		std::bind(&iks::search_data<T>, shared_from_this(), param, "right", start, end, probe_type, cb));
}

template<class T> void iks::search_data_left(const param_info& param, const T& start, const T& end, SZARP_PROBE_TYPE probe_type, std::function<void(const boost::system::error_code&, const T&) > cb) {
	m_io->post(std::bind(&iks::search_data<T>, shared_from_this(), param, "left", start, end, probe_type, cb));
}

template<class T> void iks::get_first_time(const param_info& param, std::function<void(const boost::system::error_code&, T&) > result) {
//TODO
}

template<class T> void iks::get_last_time(const param_info& param, std::function<void(const boost::system::error_code&, T&) > result) {
//TODO
}

}
