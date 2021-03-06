#include "../../../config.h"
#include "szbase_wrapper.h"
#include "sz4/exceptions.h"
#include "sz4/live_cache.h"
#include "sz4/util.h"
#include "liblog.h"
#include "ipkcontainer.h"

#include <boost/lexical_cast.hpp>

#include <conversion.h>

SzbaseObserverImpl::SzbaseObserverImpl( const std::wstring& param_name
									  , IPKContainer* ipk
									  , sz4::base* base
									  , std::function<void( void )> callback )
									  : param_name( param_name )
									  , ipk( ipk )
									  , base( base )
									  , callback( callback )
{
	TParam* tparam = ipk->GetParam( param_name );
	if( !tparam )
		throw szbase_param_not_found_error(
			(const char*)SC::S2U(L"Param " + param_name + L", does not exist.").c_str()
			);

	base->register_observer( this , std::vector<TParam*>{ tparam } );
}

void SzbaseObserverImpl::param_data_changed( TParam* )
{
	callback();
}

SzbaseObserverImpl::~SzbaseObserverImpl()
{
	TParam* tparam = ipk->GetParam( param_name );
	if( tparam )
		base->deregister_observer( this , std::vector<TParam*>{ tparam } );
}


bool SzbaseWrapper::initialized = false;
boost::filesystem::path SzbaseWrapper::szarp_dir;
std::unordered_map<std::string, std::string> SzbaseWrapper::parhub_urls;
sz4::base* SzbaseWrapper::base;
size_t SzbaseWrapper::base_cache_low_water_mark;
size_t SzbaseWrapper::base_cache_high_water_mark;
const size_t SzbaseWrapper::BASE_CACHE_LOW_WATER_MARK_DEFAULT = 128 * 1024 * 1024;
const size_t SzbaseWrapper::BASE_CACHE_HIGH_WATER_MARK_DEFAULT = 192 * 1024 * 1024;
const int SzbaseWrapper::BASE_LIVE_CACHE_RETENTION = 15 * 60;

bool SzbaseWrapper::init( const std::string& _szarp_dir , const CfgSections& locs, int live_cache_retetion, size_t base_low_water_mark, size_t base_high_water_mark)
{
	if( initialized )
		return boost::filesystem::path(_szarp_dir) == szarp_dir;

	szarp_dir = _szarp_dir;

	ParamCachingIPKContainer::Init( szarp_dir.wstring(), szarp_dir.wstring(), L"pl_PL" );
	auto ipk = IPKContainer::GetObject();

	sz4::live_cache_config live_cfg;
	live_cfg.retention = live_cache_retetion;

	for (auto loci : locs) {
		auto loc = loci.second;
		if (loc.at("type") != "szbase")
			continue;

		if (!loc.count("hub_url"))
			continue;

		auto base = loc.at("base");
		auto url = loc.at("hub_url");

		parhub_urls.insert({base, url});
		auto cfg = ipk->GetConfig((const unsigned char*)base.c_str());

		live_cfg.urls.emplace_back(url, cfg);
		parhub_urls.insert({base, url});
	}

	base = new sz4::base( szarp_dir.wstring(), ipk ,live_cfg.urls.size() ? &live_cfg : nullptr );

	SzbaseWrapper::base_cache_high_water_mark = base_high_water_mark;
	SzbaseWrapper::base_cache_low_water_mark = base_low_water_mark;

	initialized = true;

	return true;
}

SzbaseWrapper::SzbaseWrapper( const std::string& base )
	: base_name(base)
{
	std::wstring wbp( base_name.begin() , base_name.end() );
	IPKContainer::GetObject()->GetConfig( wbp );

	auto parhub_it = parhub_urls.find(base);
	if (parhub_it != parhub_urls.end()) {
		live_updater.reset(new ParhubPoller(parhub_it->second, live_values_holder));
	}
}

const std::string& SzbaseWrapper::get_base_name() const 
{
	return base_name;
}

std::wstring SzbaseWrapper::convert_string( const std::string& str ) const
{
	std::basic_string<unsigned char> ubp( str.begin() , str.end() );
	return SC::U2S( ubp );
}

void SzbaseWrapper::purge_cache()
{
	size_t size_in_bytes, blocks_count;
	auto cache = base->cache();

	cache->cache_size( size_in_bytes , blocks_count );

	if( size_in_bytes > base_cache_high_water_mark ) {
		size_t to_purge = size_in_bytes - base_cache_low_water_mark;
		sz_log(5, "Purging cache, cache size %zu, purging %zu bytes", size_in_bytes, to_purge);
		cache->remove( to_purge );
	}
}

time_t SzbaseWrapper::get_latest(
			const std::string& param ,
			ProbeType type ) const
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_get_value_error( "Cannot get latest time of param " + param + ", param not found" );

	unsigned t;
	try {
		base->get_last_time( tparam, t );
	} catch( sz4::exception& e) {
		throw szbase_get_value_error( "Cannot get latest time of param " + param + ": " + e.what() );
	}
	
	purge_cache();

	/**
	 * Round by hand because search returns probes rounded to 
	 * either 10min or 10sec, not to exact pt
	 */
	return t == unsigned( -1 ) ? -1 : round( t , type );
}

double SzbaseWrapper::get_avg(
			const std::string& param ,
			time_t time ,
			ProbeType type ) const
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_get_value_error( "Cannot get value from param " + param + ", param not found" );

	sz4::weighted_sum<double, unsigned> sum;
	try {
		base->get_weighted_sum( tparam ,
                                unsigned( time ) ,
                                unsigned( next( time , type , 1 ) ) ,
                                type.get_szarp_pt() , 
                                sum );
	} catch( sz4::exception& e ) {
		throw szbase_get_value_error( "Cannot get value from param " + param + ": " + e.what() );
	}

	purge_cache();

	return sz4::scale_value(sum.avg(), tparam);
}

namespace
{

template<class T> std::string search_data_helper( sz4::base* base,
												  TParam* param,
												  const std::string& from ,
									  			  const std::string& to ,
									  			  SearchDir dir ,
									  			  ProbeType pt )
{
									 
	T _from, _to;
	try {
		_from = boost::lexical_cast<T>( from );
		_to   = boost::lexical_cast<T>( to );
	} catch( const boost::bad_lexical_cast& ) {
		throw szbase_error( "Invalid time specification from: " + from + " to: " + to );
	}

	try{
		T result = ( dir == SearchDir::LEFT ) ?
				   base->search_data_left ( param , _from , _to , pt.get_szarp_pt() , sz4::no_data_search_condition() ) :
				   base->search_data_right( param , _from , _to , pt.get_szarp_pt() , sz4::no_data_search_condition() ) ;

		return boost::lexical_cast<std::string>(result);
	} catch( sz4::exception& e ) {
		throw szbase_error( "Search failed : " + std::string(e.what()) );
	}
}

}


std::string SzbaseWrapper::search_data( const std::string& param ,
									    const std::string& from ,
										const std::string& to ,
										TimeType time_type ,
										SearchDir dir ,
										ProbeType pt
										) const
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_param_not_found_error( "Param " + param + ", does not exist." );

	std::string result;

	switch (time_type) {
		case TimeType::NANOSECOND:
			result = search_data_helper<sz4::nanosecond_time_t>( base , tparam , from , to , dir , pt );
			break;
		case TimeType::SECOND:
			result = search_data_helper<sz4::second_time_t>    ( base , tparam , from , to , dir , pt );
			break;
	}

	purge_cache();

	return result;
}

namespace {

template<class value_time, class time_type>
std::ostream& get_data( sz4::base*       base ,
					  TParam*          param ,
					  time_type        from ,
					  time_type        to ,
					  SZARP_PROBE_TYPE pt ,
					  std::ostream&    os )
{
	bool first = true;

	while ( from < to )
	{
		time_type next = szb_move_time( from , 1 , pt , 0 );

		sz4::weighted_sum< value_time, time_type > sum;
		base->get_weighted_sum( param , from , next , pt , sum );

		if (first)
			first = false;
		else
			os << " ";
	
		os << sum._sum() << " " << sum.weight() << " " << sum.no_data_weight() << " " << sum.fixed();

		from = next;
	}

	return os;
}

template<class time_type>
std::ostream& get_data( sz4::base*       base ,
					  TParam*            param ,
					  const std::string& from ,
					  const std::string& to ,
					  ValueType          vt ,
					  SZARP_PROBE_TYPE   pt ,
					  std::ostream&      os )
{

	time_type _from, _to;
	try{
		_from = boost::lexical_cast<time_type>( from );
		_to   = boost::lexical_cast<time_type>( to );
	} catch( const boost::bad_lexical_cast& ) {
		throw szbase_error( "Invalid time specification from: " + from + " to: " + to );
	}

	switch (vt) {
		case ValueType::DOUBLE:
			get_data<double       , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::FLOAT:
			get_data<float        , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::INT:
			get_data<int          , time_type>( base , param , _from , _to , pt , os );
			break;
		case ValueType::SHORT:
			get_data<short        , time_type>( base , param , _from , _to , pt , os );
			break;
	}

	return os;
}

}

std::string SzbaseWrapper::get_data( const std::string& param ,
									 const std::string& from ,
									 const std::string& to ,
									 ValueType value_type ,
									 TimeType time_type ,
									 ProbeType pt ) const
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	std::ostringstream ss;

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );
	if( !tparam )
		throw szbase_param_not_found_error( "Param " + param + ", does not exist." );

	try{
		switch (time_type) {
			case TimeType::NANOSECOND:
				::get_data<sz4::nanosecond_time_t>( base , tparam ,
								    from , to ,
								    value_type , pt.get_szarp_pt() ,
								    ss );
				break;
			case TimeType::SECOND:
				::get_data<sz4::second_time_t>    ( base , tparam ,
								    from , to ,
								    value_type , pt.get_szarp_pt() ,
								    ss );
				break;
		}
	} catch ( sz4::exception& e ) {
		throw szbase_error( "Cannot get data for param " + param + ": " + e.what() );
	}

	purge_cache();

	return ss.str();
}

SzbaseObserverToken SzbaseWrapper::register_observer( const std::string& param , boost::optional<ProbeType> pt, std::function<void( void )> callback )
{
	if( !SzbaseWrapper::is_initialized() )
		throw szbase_init_error("Szbase not initialized");

	TParam* tparam = IPKContainer::GetObject()->GetParam( convert_string( base_name + ":" + param ) );

	if( !tparam )
		throw szbase_param_not_found_error( "Param " + param + ", does not exist." );

	if (pt && *pt == ProbeType::Type::LIVE && tparam->GetType() == ParamType::REAL) {
		auto initial_value = [this, param]() { return get_updated_value(param, ProbeType::Type::S); };

		auto ipc_ind = tparam->GetIpcInd();
		auto prec = tparam->GetPrec();
		return live_values_holder.add_observer(param, ipc_ind, prec, callback, initial_value);
	} else {
		return std::make_shared<SzbaseObserverImpl>(convert_string( base_name + ":" + param ), IPKContainer::GetObject(), base, callback);
	}
}

double SzbaseWrapper::get_updated_value( const std::string& param , ProbeType ptype ) const {
	if (ptype == ProbeType::Type::LIVE && live_values_holder.has_param(param)) {
		return get_live_val(param);
	}

	// if non-live or calculated using formulas
	time_t t = get_latest( param, ptype );
	time_t ptime = SzbaseWrapper::round( t , ptype );
	if (t == ptime) {
		//special case, account for the fact that in this case we
		//need to ask for one second (because currently IKS operates
		//with resolution of up to 1 sec) starting just before
		//latest time
		ptime = sz4::time_just_before( sz4::second_time_t( t ) );
		ptype = ProbeType(ProbeType::Type::S);
	}

	return get_avg(param, ptime, ptype);
}

namespace {

bool create_param_name( const std::wstring& original_name , const std::wstring& token, std::wstring& new_name )
{
	auto colon_count = std::count( original_name.begin() , original_name.end() , L':');
	if (colon_count != 2)
		return false;

	
	auto i = std::find( original_name.begin() , original_name.end() , L':' ) + 1;
	new_name.append( original_name.begin() , i );
	new_name.append( token );
	new_name.append( std::find( i , original_name.end() , L':' ) , original_name.end() );

	return true;

}

bool create_param_name_in_formula( const std::wstring& original_name , const std::wstring& token, std::wstring& new_name )
{
	auto colon_count = std::count( original_name.begin() , original_name.end() , L':');
	if (colon_count != 3)
		return false;

	auto c0 = std::find( original_name.begin() , original_name.end() , L':' ) + 1;
	auto c1 = std::find( c0 , original_name.end() , L':' ) + 1;
	auto c2 = std::find( c1 , original_name.end() , L':' );

	if (*c0 != L'*')
		return false;

	new_name.append( original_name.begin() , c0 );
	new_name.append( c0 + 1, c1 );
	new_name.append( token );
	new_name.append( c2 , original_name.end() );

	return true;
}

}

time_t SzbaseWrapper::next( time_t t , ProbeType pt , int num )
{
	return szb_move_time( t , num , pt.get_szarp_pt() , pt.get_len() );
}

time_t SzbaseWrapper::round( time_t t , ProbeType pt )
{
	return szb_round_time( t , pt.get_szarp_pt() , pt.get_len() );
}

